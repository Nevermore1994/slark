if(${CMAKE_VERSION} VERSION_LESS "3.20")
    message(FATAL_ERROR "CMake >= 3.20 required")
endif()

cmake_minimum_required(VERSION 3.20)

project(slark)

# Export compile commands for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#
# # Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wcast-align -Wcast-qual -Wconversion -Wdisabled-optimization -Wendif-labels -Wfloat-equal -Winit-self -Winline -Wmissing-include-dirs -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wpacked -Wpointer-arith -Wredundant-decls -Wshadow -Wsign-promo -Wwrite-strings -Wno-variadic-macros -Wno-unknown-pragmas -Wno-undef")

# Set thread library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# print current path
message(STATUS "current path: " ${CMAKE_CURRENT_SOURCE_DIR})

# build options
set(BUILD_PLATFORM "PC" CACHE STRING "build platform")
option(DISABLE_TEST "disable test" OFF)
option(DISABLE_HTTP "disable http" OFF)
option(DISABLE_CORE "disable core module (for testing base/http only)" OFF)

# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions(-DRELEASE)
else()
    add_definitions(-DDEBUG)
endif()

# add src subdirectories
add_subdirectory(src/include)
add_subdirectory(src/base)

if(NOT DISABLE_CORE)
    add_subdirectory(src/core)
endif()

# create shared library from object libraries
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

if(${BUILD_PLATFORM} STREQUAL "iOS")
    message(STATUS "add iOS interface")
    include(CMake-iOS.cmake)
    add_ios_sources(${PROJECT_NAME})
    configure_ios_target(${PROJECT_NAME})
elseif(${BUILD_PLATFORM} STREQUAL "Android")
    message(STATUS "add Android interface")
    include(CMake-Android.cmake)
    add_android_sources(${PROJECT_NAME})
endif()

if(NOT DISABLE_HTTP)
    message(STATUS "enable http")
    add_subdirectory(src/http)
    target_link_libraries(${PROJECT_NAME} PUBLIC slark_http)
else()
    message(STATUS "disable http")
endif()

if(NOT DISABLE_TEST)
    message(STATUS "enable testing")
    enable_testing()
    include(cmake/gtest.cmake)
    add_subdirectory(test)
else()
    message(STATUS "disable testing")
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC ${CMAKE_THREAD_LIBS_INIT})
