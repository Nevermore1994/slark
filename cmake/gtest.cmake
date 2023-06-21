cmake_minimum_required(VERSION 3.20)

# Google Test settings
include(FetchContent)
FetchContent_Declare( googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.11.0
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)