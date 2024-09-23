cmake_minimum_required(VERSION 3.20)

project(slark)

#C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
#
## Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wcast-align -Wcast-qual -Wconversion -Wdisabled-optimization -Wendif-labels -Wfloat-equal -Winit-self -Winline -Wmissing-include-dirs -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wpacked -Wpointer-arith -Wredundant-decls -Wshadow -Wsign-promo -Wvariadic-macros -Wwrite-strings -Wno-variadic-macros")

# Set thread library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# print current path
message(STATUS "current path: " ${CMAKE_CURRENT_SOURCE_DIR})

macro(add_framework FRAME_WORK_NAME APPEND_NAME)
    find_library(FRAMEWORK_${FRAME_WORK_NAME}
            NAMES ${FRAME_WORK_NAME}
            PATHS ${CMAKE_OSX_SYSROOT}/System/Library
            PATH_SUFFIXES Frameworks
            NO_DEFAULT_PATH)
    if( ${FRAMEWORK_${FRAME_WORK_NAME}} STREQUAL FRAMEWORK_${FRAME_WORK_NAME}-NOTFOUND)
        MESSAGE(ERROR ": Framework ${FRAME_WORK_NAME} not found")
    else()
        target_link_libraries(${APPEND_NAME} "${FRAMEWORK_${FRAME_WORK_NAME}}")
        MESSAGE(STATUS "Framework ${FRAME_WORK_NAME} found at ${FRAMEWORK_${FRAME_WORK_NAME}}")
    endif()
endmacro(add_framework)

# add core library
set(BUILD_PLATFORM "PC" CACHE STRING "build platform")
option(DISABLE_TEST "disable test" OFF)

function(read_config)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/config.json CONFIG_JSON_STRING)

    #parse build platform
    string(JSON BUILD_PLATFORM_VALUE GET ${CONFIG_JSON_STRING} platform)
    set(BUILD_PLATFORM ${BUILD_PLATFORM_VALUE} CACHE STRING "build platform" FORCE)

    #parse and set build type
    string(JSON BUILD_TYPE GET ${CONFIG_JSON_STRING} build_type)
    message(STATUS "build:${BUILD_TYPE}")
    if(${BUILD_TYPE} STREQUAL "Debug")
        set(CMAKE_BUILD_TYPE DEBUG PARENT_SCOPE)
        add_definitions(-DDEBUG) #set DEBUG = 1
    else()
        set(CMAKE_BUILD_TYPE RELEASE PARENT_SCOPE)
        add_definitions(-DRELEASE) #set RELEASE = 1
    endif()

    #parse and set output type
    string(JSON BUILD_OUTPUT_TYPE GET ${CONFIG_JSON_STRING} output)
    message(STATUS "output:${BUILD_OUTPUT_TYPE}")
    if(${BUILD_OUTPUT_TYPE} STREQUAL "exe")
        add_executable(${PROJECT_NAME} "")
        target_sources(${PROJECT_NAME} PUBLIC src/main.cpp)
        source_group("SourceFiles" FILES src/main.cpp)
    else()
        add_library(${PROJECT_NAME} SHARED "")
    endif()

    if(${BUILD_PLATFORM_VALUE} STREQUAL "iOS" OR ${BUILD_PLATFORM_VALUE} STREQUAL "Android")
        set(DISABLE_TEST ON CACHE BOOL "Disable test" FORCE)
    else()
        string(JSON DISABLE_TEST_VALUE GET ${CONFIG_JSON_STRING} disable_test)
        set(DISABLE_TEST ${DISABLE_TEST_VALUE} CACHE BOOL "Disable test" FORCE)
    endif()
endfunction()

read_config()

#add base library
add_subdirectory(src/base)

#add core library
add_subdirectory(src/core)
target_link_libraries(${PROJECT_NAME} PRIVATE core)

if(${BUILD_PLATFORM} STREQUAL "iOS")
    message(STATUS "add iOS interface")
    add_subdirectory(src/interface/iOS)
    target_link_libraries(${PROJECT_NAME} PUBLIC iOS_INTERFACE)
elseif (${BUILD_PLATFORM} STREQUAL "Android")

endif ()

if(NOT DISABLE_TEST)
    message(STATUS "enable testing")
    enable_testing()
    include(cmake/gtest.cmake)
    add_subdirectory(test)
else()
    message(STATUS "disable testing")
endif()


target_link_libraries(${PROJECT_NAME}
        PRIVATE
            base
        PUBLIC
            ${CMAKE_THREAD_LIBS_INIT}
        )

#target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address)
