cmake_minimum_required(VERSION 3.20)

# Google Test settings
include(FetchContent)

# 统一 FetchContent 缓存目录
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/.fetchcontent_cache" CACHE PATH "" FORCE)

FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)