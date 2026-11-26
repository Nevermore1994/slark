cmake_minimum_required(VERSION 3.20)

# Google Test settings
include(FetchContent)

# 默认共享源码缓存；独立 sanitizer 构建可通过 SLARK_FETCHCONTENT_BASE_DIR 隔离二进制产物。
set(SLARK_FETCHCONTENT_BASE_DIR
    "${CMAKE_SOURCE_DIR}/.fetchcontent_cache"
    CACHE PATH "FetchContent cache directory")
set(FETCHCONTENT_BASE_DIR "${SLARK_FETCHCONTENT_BASE_DIR}" CACHE PATH "" FORCE)

FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    foreach(gtest_target gtest gtest_main gmock gmock_main)
        target_compile_options(${gtest_target} PRIVATE -Wno-character-conversion)
    endforeach()
endif()