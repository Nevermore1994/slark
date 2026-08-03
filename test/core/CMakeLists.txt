cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

project(Core_test)

set(CORE_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/core)
set(DEMUXER_SRC_DIR ${CORE_SRC_DIR}/demuxer)

file(GLOB TEST_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# PlayerIntegrationTest 单独作为 CoreIntegration_test target，从 Core_test 中排除
list(REMOVE_ITEM TEST_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/PlayerIntegrationTest.cpp
)
# Fuzz 测试移到 test/fuzz/ 统一管理
list(REMOVE_ITEM TEST_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/M3U8ParserFuzzTest.cpp
)

# 直接包含需要的源文件，避免编译整个 slark_core
set(CORE_TEST_SOURCES
    ${CORE_SRC_DIR}/PlayerStateMachine.cpp
    ${CORE_SRC_DIR}/MediaDefs.cpp
    ${CORE_SRC_DIR}/MediaUtil.cpp
    ${CORE_SRC_DIR}/MediaPlaylist.cpp
    ${CORE_SRC_DIR}/NotifyCenter.cpp
    ${CORE_SRC_DIR}/Event.cpp
    ${CORE_SRC_DIR}/pipeline/BufferMonitor.cpp
    ${CORE_SRC_DIR}/perf/PlaySession.cpp
    ${DEMUXER_SRC_DIR}/M3U8Parser.cpp
    ${DEMUXER_SRC_DIR}/DemuxerManager.cpp
    ${DEMUXER_SRC_DIR}/Mp4Box.cpp
    ${DEMUXER_SRC_DIR}/Mp4Demuxer.cpp
    ${DEMUXER_SRC_DIR}/HLSDemuxer.cpp
    ${DEMUXER_SRC_DIR}/WavDemuxer.cpp
    ${CORE_SRC_DIR}/HttpStream.cpp
    ${CORE_SRC_DIR}/HLSStream.cpp
)

add_executable(${PROJECT_NAME} ${TEST_FILES}
    ${CORE_TEST_SOURCES}
)

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
    PARALLEL_LEVEL 0
    LABELS "core"
)

add_custom_target(run_core_test_parallel
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j$(nproc) -L core
    DEPENDS ${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running core tests in parallel"
)

# ─── Player 对外接口集成测试（链接完整 slark_core）───────────────────────────
if(NOT DISABLE_CORE)
    project(CoreIntegration_test)

    add_executable(${PROJECT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/PlayerIntegrationTest.cpp
    )

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

    add_test(NAME PlayerIntegrationTest
        COMMAND ${PROJECT_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    set_tests_properties(PlayerIntegrationTest PROPERTIES
        TIMEOUT 300
        LABELS "integration"
    )
endif()