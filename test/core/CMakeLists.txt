cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

PROJECT(Core_test)

set(CORE_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/core)

file(GLOB TEST_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

add_executable(${PROJECT_NAME} ${TEST_FILES}
    ${CORE_SRC_DIR}/pipeline/EpochFilter.cpp
    ${CORE_SRC_DIR}/PlayerStateMachine.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
    ${CORE_SRC_DIR}
    ${CORE_SRC_DIR}/pipeline
    ${CORE_SRC_DIR}/demuxer
    ${CORE_SRC_DIR}/audio
    ${CORE_SRC_DIR}/codec
    ${CORE_SRC_DIR}/perf
    ${CMAKE_CURRENT_SOURCE_DIR}/../../internal
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    $<$<BOOL:${APPLE}>:${CMAKE_CURRENT_SOURCE_DIR}/../../src/platform/iOS>
)

target_link_libraries(${PROJECT_NAME}
    slark_base
    slark_internal
    gtest_main
    pthread)

gtest_discover_tests(${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES
    TIMEOUT 300
    PARALLEL_LEVEL 0
    LABELS "core"
)

add_custom_target(run_core_test_parallel
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j$(nproc) -L core
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running core tests in parallel"
)
