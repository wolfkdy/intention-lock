#include <assert.h>
#include "mgl/mgl_mgr.h"
#include "mgl/mgl.h"
#include "mgl/lock_defines.h"

namespace mgl {

bool isConflict(uint16_t modes, LockMode mode) {
    static int16_t conflictTable[enum2Int(LockMode::LOCK_MODE_NUM)] =
            {0,  // NONE
             1<<enum2Int(LockMode::LOCK_X), // IS
             (1<<enum2Int(LockMode::LOCK_S))|(1<<enum2Int(LockMode::LOCK_X)),  // IX
             (1<<enum2Int(LockMode::LOCK_IX))|(1<<enum2Int(LockMode::LOCK_X)), // S
             (1<<enum2Int(LockMode::LOCK_IS))|(1<<enum2Int(LockMode::LOCK_IX))
             |(1<<enum2Int(LockMode::LOCK_S))|(1<<enum2Int(LockMode::LOCK_X))}; // X
    int16_t modeInt = enum2Int(mode);
    return (conflictTable[modeInt]&modes) != 0;
}

LockSchedCtx::LockSchedCtx()
    :_runningModes(0),
     _pendingModes(0),
     _runningRefCnt(enum2Int(LockMode::LOCK_MODE_NUM), 0),
     _pendingRefCnt(enum2Int(LockMode::LOCK_MODE_NUM), 0) {
}

// NOTE(deyukong): if compitable locks come endlessly, and we always schedule
// compitable locks first. Then the _pendingList will have no chance to schedule.
void LockSchedCtx::lock(MGLock *core) {
    auto mode = core->getMode();
    if (isConflict(_runningModes, mode) || _pendingList.size() >= 1) {
        auto it = _pendingList.insert(_pendingList.end(), core);
        incrPendingRef(mode);
        core->setLockResult(LockRes::LOCKRES_WAIT, it);
    } else {
        auto it = _runningList.insert(_runningList.end(), core);
        incrRunningRef(mode);
        core->setLockResult(LockRes::LOCKRES_OK, it);
    }
}

void LockSchedCtx::schedPendingLocks() {
    std::list<MGLock*>::iterator it = _pendingList.begin();
    while (it != _pendingList.end()) {
        MGLock* tmpLock = *it;
        if (isConflict(_runningModes, tmpLock->getMode())) {
            it++;
            continue;
        }
        incrRunningRef(tmpLock->getMode());
        decPendingRef(tmpLock->getMode());
        auto runningIt = _runningList.insert(_runningList.end(), tmpLock);
        it = _pendingList.erase(it);
        tmpLock->setLockResult(LockRes::LOCKRES_OK, runningIt);
        tmpLock->notify();
    }
}

void LockSchedCtx::unlock(MGLock *core) {
    auto mode = core->getMode();
    if (core->getStatus() == LockRes::LOCKRES_OK) {
        _runningList.erase(core->getLockIter());
        decRunningRef(mode);
        core->releaseLockResult();
        if (_runningModes != 0) {
            return;
        }
        assert(_runningList.size() == 0);
        schedPendingLocks();
    } else if (core->getStatus() == LockRes::LOCKRES_WAIT) {
        _pendingList.erase(core->getLockIter());
        decPendingRef(mode);
        core->releaseLockResult();
        // no need to schedPendingLocks here
        assert((_pendingModes == 0 && _pendingList.size() == 0) ||
                (_pendingModes != 0 && _pendingList.size() != 0));
    } else {
        assert(0);
    }
}

void LockSchedCtx::incrPendingRef(LockMode mode) {
    auto modeInt = enum2Int(mode);
    ++_pendingRefCnt[modeInt];
    if (_pendingRefCnt[modeInt] == 1) {
        assert((_pendingModes&(1<<modeInt)) == 0);
        _pendingModes |= (1<<modeInt);
    }
}

void LockSchedCtx::decPendingRef(LockMode mode) {
    auto modeInt = enum2Int(mode);
    assert(_pendingRefCnt[modeInt] != 0);
    --_pendingRefCnt[modeInt];
    if (_pendingRefCnt[modeInt] == 0) {
        assert((_pendingModes&(1<<modeInt)) != 0);
        _pendingModes &= ~(1<<modeInt);
    }
}

void LockSchedCtx::incrRunningRef(LockMode mode) {
    auto modeInt = enum2Int(mode);
    ++_runningRefCnt[modeInt];
    if (_runningRefCnt[modeInt] == 1) {
        assert((_runningModes&(1<<modeInt)) == 0);
        _runningModes |= (1<<modeInt);
    }
}

void LockSchedCtx::decRunningRef(LockMode mode) {
    auto modeInt = enum2Int(mode);
    assert(_runningRefCnt[modeInt] != 0);
    --_runningRefCnt[modeInt];
    if (_runningRefCnt[modeInt] == 0) {
        assert((_runningModes&(1<<modeInt)) != 0);
        _runningModes &= ~(1<<modeInt);
    }
}

MGLockMgr& MGLockMgr::getInstance() {
    static MGLockMgr mgr;
    return mgr;
}

void MGLockMgr::lock(MGLock *core) {
    uint64_t hash = core->getHash();
    LockShard& shard = _shards[hash%SHARD_NUM];
    std::lock_guard<std::mutex> lk(shard.mutex);
    auto iter = shard.map.find(core->getTarget());
    if (iter == shard.map.end()) {
        LockSchedCtx tmp;
        auto insertResult =
            shard.map.emplace(
                core->getTarget(), std::move(tmp));
        iter = insertResult.first;
    }
    iter->second.lock(core);
    return;
}

void MGLockMgr::unlock(MGLock *core) {
    uint64_t hash = core->getHash();
    LockShard& shard = _shards[hash%SHARD_NUM];
    std::lock_guard<std::mutex> lk(shard.mutex);

    assert(core->getStatus() == LockRes::LOCKRES_WAIT
        || core->getStatus() == LockRes::LOCKRES_OK);

    auto iter = shard.map.find(core->getTarget());
    assert(iter != shard.map.end());
    iter->second.unlock(core);
    return;
}

}
