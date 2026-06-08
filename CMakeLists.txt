if(${CMAKE_VERSION} VERSION_LESS "3.20")
    message(FATAL_ERROR "CMake >= 3.20 required")
endif()

cmake_minimum_required(VERSION 3.20)

project(slark)

set(SLARK_PROJECT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wcast-align -Wcast-qual -Wconversion -Wdisabled-optimization -Wendif-labels -Wfloat-equal -Winit-self -Winline -Wmissing-include-dirs -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wpacked -Wpointer-arith -Wredundant-decls -Wshadow -Wsign-promo -Wwrite-strings -Wno-variadic-macros -Wno-unknown-pragmas -Wno-undef")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

message(STATUS "current path: " ${CMAKE_CURRENT_SOURCE_DIR})

set(BUILD_PLATFORM "PC" CACHE STRING "build platform")
option(DISABLE_TEST "disable test" OFF)
option(DISABLE_HTTP "disable http" OFF)
option(DISABLE_CORE "disable core module (for testing base/http only)" OFF)

if(${BUILD_PLATFORM} STREQUAL "Android")
    set(SLARK_ANDROID ON)
    add_compile_definitions(SLARK_ANDROID)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions(-DRELEASE)
else()
    add_definitions(-DDEBUG)
endif()

# =============================================================================
# iOS：不创建中间 OBJECT target，所有源文件直接打入 slark.framework
# =============================================================================
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(DISABLE_TEST ON CACHE BOOL "disable test" FORCE)
    add_subdirectory(include)
    add_subdirectory(internal)
    include(CMake-iOS.cmake)
    add_ios_framework()

# =============================================================================
# 其他平台
# =============================================================================
else()
    add_subdirectory(include)
    add_subdirectory(internal)
    add_subdirectory(src/base)

    if(NOT DISABLE_HTTP)
        message(STATUS "enable http")
        add_subdirectory(src/http)
    else()
        message(STATUS "disable http")
    endif()

    if(NOT DISABLE_CORE)
        add_subdirectory(src/core)
    endif()

    if(NOT DISABLE_CORE)
        add_library(${PROJECT_NAME} SHARED
            $<TARGET_OBJECTS:slark_base>
            $<TARGET_OBJECTS:slark_core>
        )
        target_link_libraries(${PROJECT_NAME} PUBLIC slark_core slark_base slark_include)
    else()
        add_library(${PROJECT_NAME} SHARED
            $<TARGET_OBJECTS:slark_base>
        )
        target_link_libraries(${PROJECT_NAME} PUBLIC slark_base slark_include)
    endif()

    if(${BUILD_PLATFORM} STREQUAL "Android")
        message(STATUS "add Android interface")
        include(CMake-Android.cmake)
        add_android_interface()

        # JNI 函数由 JVM 动态查找，libslark.so 中没有代码直接引用它们，
        # 必须使用 --whole-archive 强制链接器保留所有静态库中的符号
        target_link_libraries(${PROJECT_NAME}
            PUBLIC
            -Wl,--whole-archive
            slark_jni
            slark_jni_common
            -Wl,--no-whole-archive
        )
    endif()

    if(NOT DISABLE_HTTP)
        target_link_libraries(${PROJECT_NAME} PUBLIC slark_http)
    endif()

    target_link_libraries(${PROJECT_NAME} PUBLIC ${CMAKE_THREAD_LIBS_INIT})

    if(NOT DISABLE_TEST)
        message(STATUS "enable testing")
        enable_testing()
        include(cmake/gtest.cmake)
        add_subdirectory(test)
    else()
        message(STATUS "disable testing")
    endif()
endif()