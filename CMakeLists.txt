cmake_minimum_required(VERSION 3.1)

include(CMakeForceCompiler)
include(CMakePrintHelpers)

#C++ standard
set(CMAKE_CXX_STANDARD 17)

message(STATUS "current path: " ${CMAKE_CURRENT_SOURCE_DIR})

project(slark)

#set thread library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

#source file path
set(BASE_INCLUDE_DIR slark/base/headers)
set(THIRD_PARTY_INCLUDE_DIR slark/third_party)
set(CORE_INCLUDE_DIR slark/core/headers)
set(AUDIO_INCLUDE_DIR slark/core/audio/headers)

#base src files
aux_source_directory(slark/base/src BASE_DIR_SRCS)
#core src files
aux_source_directory(slark/core/src CORE_DIR_SRCS)
#core base testing files
aux_source_directory(slark/base/test/ BASE_TEST_DIR_SRCS)
#core core testing files
aux_source_directory(slark/core/main.cpp CORE_TEST_DIR_SRCS)
#audio src files
aux_source_directory(slark/core/audio AUDIO_DIR_SRCS)

#compilation options
set(CORE_LIBRARY 0)
set(AUDIO_LIBRARY 0)

set(ALL_SOURCE ${BASE_DIR_SRCS})
set(ALL_HEADER ${BASE_INCLUDE_DIR})
list(APPEND ALL_HEADER ${THIRD_PARTY_INCLUDE_DIR})

#release
set(RELEASE_BASE_LIBRARY 0)
set(RELEASE_CORE_LIBRARY 1)
set(RELEASE_ALL_LIBRARY 100)
#test type
set(TESTING_BASE_TYPE 101)
set(TESTING_CORE_TYPE 102)
set(TESTING_ALL_TYPE 103)

#set build type
set(TESTING_TYPE ${TESTING_CORE_TYPE})

#default setting
set(BUILD_TYPE RELEASE) #default release
set(MAIN_FILE)

#parse config
if (TESTING_TYPE GREATER ${RELEASE_ALL_LIBRARY})
    set(BUILD_TYPE DEBUG)
endif ()

if (TESTING_TYPE EQUAL ${TESTING_BASE_TYPE})
    #base test
    set(MAIN_FILE slark/base/test/main.cpp)
    list(APPEND ALL_SOURCE ${BASE_TEST_DIR_SRCS})
elseif (TESTING_TYPE EQUAL ${TESTING_CORE_TYPE})
    #core test
    set(MAIN_FILE slark/core/main.cpp)
    set(CORE_LIBRARY 1)
    list(APPEND ALL_SOURCE ${CORE_TEST_DIR_SRCS})
elseif (TESTING_TYPE EQUAL ${TESTING_ALL_TYPE})
    #all test
    set(MAIN_FILE slark/core/main.cpp)
    set(CORE_LIBRARY 1)
    set(AUDIO_LIBRARY 1)
    list(APPEND ALL_SOURCE ${CORE_TEST_DIR_SRCS})
endif ()

if (CORE_LIBRARY EQUAL 1)
    list(APPEND ALL_SOURCE ${CORE_DIR_SRCS})
    list(APPEND ALL_HEADER ${CORE_INCLUDE_DIR})
endif ()

if (AUDIO_LIBRARY EQUAL 1)
    list(APPEND ALL_SOURCE ${AUDIO_DIR_SRCS})
    list(APPEND ALL_HEADER ${CORE_INCLUDE_DIR})
endif ()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
set(CMAKE_BUILD_TYPE ${BUILD_TYPE})

#set search header file
include_directories(${ALL_HEADER})

message(STATUS "current build: " ${CMAKE_BUILD_TYPE} " build type:" ${TESTING_TYPE})

if (CMAKE_BUILD_TYPE STREQUAL DEBUG)
    add_definitions(-DDEBUG) #set DEBUG = 1
    add_executable(${PROJECT_NAME} ${MAIN_FILE} ${ALL_SOURCE})
    message("build testing....")
else ()
    add_library(${PROJECT_NAME} ${ALL_SOURCE})
endif ()

target_link_libraries(${PROJECT_NAME}  ${CMAKE_THREAD_LIBS_INIT})

