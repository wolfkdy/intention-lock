add_library(mgl mgl.cpp mgl_mgr.cpp)

add_executable(mgl_test mgl_test.cpp)
target_link_libraries(mgl_test mgl gtest_main)
