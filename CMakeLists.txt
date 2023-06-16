cmake_minimum_required(VERSION 3.1)

INCLUDE(CMakeForceCompiler)

set(CMAKE_CXX_STANDARD 17)

set(THREADS_PREFER_PTHREAD_FLAG ON)

project(slark)

set(CORE_INCLUDE_DIR slark/core/headers)
set(BASE_INCLUDE_DIR slark/base/headers)
set(THIRD_PARTY_INCLUDE_DIR slark/third_party)

aux_source_directory(slark/base/src BASE_DIR_SRCS)
aux_source_directory(slark/core/src CORE_DIR_SRCS)
aux_source_directory(slark/core/demuxer DEMUX_DIR_SRCS)

file(GLOB ALL_SOURCE ${CORE_DIR_SRCS} ${DEMUX_DIR_SRCS} ${BASE_DIR_SRCS})

find_package(Threads REQUIRED)

include_directories(${CORE_INCLUDE_DIR} ${BASE_INCLUDE_DIR} ${THIRD_PARTY_INCLUDE_DIR})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")

set(CMAKE_BUILD_TYPE Debug)

if (CMAKE_BUILD_TYPE STREQUAL Debug)
    ADD_DEFINITIONS(-DDEBUG)
endif ()

add_executable(${PROJECT_NAME} ./main.cpp ${ALL_SOURCE})

#add_library(${PROJECT_NAME} ${ALL_SOURCE})

target_link_libraries(${PROJECT_NAME}  ${CMAKE_THREAD_LIBS_INIT})

