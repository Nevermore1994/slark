cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

project(Platform_test)

set(CORE_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/core)

file(GLOB TEST_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(${PROJECT_NAME} ${TEST_FILES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
    ${CORE_SRC_DIR}
    ${CORE_SRC_DIR}/demuxer
    ${CORE_SRC_DIR}/pipeline
    ${CORE_SRC_DIR}/audio
    ${CORE_SRC_DIR}/codec
    ${CORE_SRC_DIR}/perf
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/http
    ${CMAKE_CURRENT_SOURCE_DIR}/../../internal
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

target_link_libraries(${PROJECT_NAME}
    slark_core
    slark_base
    slark_internal
    gtest_main
    gmock
    pthread)

if(NOT DISABLE_HTTP)
    target_link_libraries(${PROJECT_NAME} slark_http)
endif()

gtest_discover_tests(${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES
    TIMEOUT 300
    LABELS "platform"
)
