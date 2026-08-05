cmake_minimum_required(VERSION 3.20)
project(PlayerMediaIntegration_test)

# iOS 模拟器集成测试构建：配合 ios.toolchain.cmake + PLATFORM=SIMULATORARM64
# 源文件集与 CMake-iOS.cmake 的 slark.framework 保持一致，额外加入 gtest 与测试文件

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-old-style-cast")

find_package(Threads REQUIRED)

# googletest：复用主工程已下载源码，独立构建目录避免生成器冲突
include(FetchContent)
set(FETCHCONTENT_SOURCE_DIR_GOOGLETEST
    "${ROOT_DIR}/.fetchcontent_cache/googletest-src" CACHE PATH "" FORCE)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- 源文件收集（与 CMake-iOS.cmake 一致）---
set(BASE_DIR "${ROOT_DIR}/src/base")
set(CORE_DIR "${ROOT_DIR}/src/core")
set(HTTP_DIR "${ROOT_DIR}/src/http")
set(IOS_DIR "${ROOT_DIR}/src/platform/iOS")

file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS
    "${BASE_DIR}/*.cpp" "${BASE_DIR}/*.hpp"
    "${CORE_DIR}/*.cpp" "${CORE_DIR}/*.hpp"
    "${HTTP_DIR}/*.cpp" "${HTTP_DIR}/*.hpp"
    "${IOS_DIR}/*.cpp" "${IOS_DIR}/*.hpp"
    "${IOS_DIR}/*.m" "${IOS_DIR}/*.mm"
)

# 禁用 HTTPS：排除 SSL 源，避免依赖 OpenSSL 静态库
list(FILTER ALL_SOURCES EXCLUDE REGEX ".*SSL.*")

add_executable(PlayerMediaIntegration_test
    ${ALL_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/../core/PlayerMediaIntegrationTest.cpp"
)

target_include_directories(PlayerMediaIntegration_test PRIVATE
    "${ROOT_DIR}/include"
    "${ROOT_DIR}/internal"
    "${BASE_DIR}"
    "${BASE_DIR}/coroutine"
    "${CORE_DIR}"
    "${CORE_DIR}/audio"
    "${CORE_DIR}/codec"
    "${CORE_DIR}/demuxer"
    "${CORE_DIR}/pipeline"
    "${CORE_DIR}/perf"
    "${HTTP_DIR}"
    "${IOS_DIR}"
    "${IOS_DIR}/interface"
)

target_compile_definitions(PlayerMediaIntegration_test PUBLIC SLARK_IOS)
target_compile_definitions(PlayerMediaIntegration_test PRIVATE GLES_SILENCE_DEPRECATION)

set_target_properties(PlayerMediaIntegration_test PROPERTIES
    MACOSX_BUNDLE_IDENTIFIER com.slark.PlayerMediaIntegrationTest
)

if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(PlayerMediaIntegration_test PRIVATE DEBUG)
endif()

target_link_libraries(PlayerMediaIntegration_test PRIVATE gtest_main gmock Threads::Threads)

# iOS 系统框架（与 CMake-iOS.cmake 一致；AudioUnit 归属 AudioToolBox，模拟器无独立 framework）
foreach(fw IN ITEMS
        Foundation CoreFoundation AVFoundation VideoToolBox AudioToolBox
        CoreMedia CoreVideo CoreGraphics Metal QuartzCore UIKit
        OpenGLES GLKit)
    target_link_libraries(PlayerMediaIntegration_test PUBLIC "-framework ${fw}")
endforeach()
