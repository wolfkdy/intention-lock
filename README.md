# multiple-granularity-lock

A pure c++ implementaion of Intention Lock(IS/IX/S/X).
---
Currently, a single lock can only lock a single target for simple.
Lock mode can not be dynamicly downgraded/upgraded, you can use
another lock instead.

It's NOT required that lock/unlock in the same thread.
But you should explicitly call unlock if you called lock, no matter
successes or not. An unlocked MGLock will lead to crash on its
destructor, you should wrap your own RAII class above MGL.

On each lock, two lists are maintained, the running-list and
the conflict-list. If the incoming lock requirement is compitable
with the running-list, the lock is granted immediatly, otherwise
it is added into the conflict-list.

In order to avoid starvation, If the conflict list is not empty,
new requests are added into conflict-list even if it's compitable
with the running-list.

# TODO
1) deadlock detect
