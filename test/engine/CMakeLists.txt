cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

project(Engine_test)

set(CORE_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/core)
set(PERF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/perf)
set(ENGINE_SRC_DIR ${CORE_SRC_DIR}/engine)

file(GLOB TEST_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# 与 Core_test 一致：直接列出源文件，不链接 slark_core。
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS
    ${ENGINE_SRC_DIR}/*.cpp
    ${ENGINE_SRC_DIR}/source/*.cpp
    ${ENGINE_SRC_DIR}/pipeline/*.cpp
)

set(DEMUXER_SRC_DIR ${CORE_SRC_DIR}/demuxer)

# 复用的旧代码：demuxer 与 stream，不引入旧 pipeline / PlayerImpl。
set(LEGACY_SOURCES
    ${CORE_SRC_DIR}/media/MediaDefs.cpp
    ${CORE_SRC_DIR}/media/MediaUtil.cpp
    ${CORE_SRC_DIR}/stream/MediaPlaylist.cpp
    ${CORE_SRC_DIR}/stream/HttpStream.cpp
    ${CORE_SRC_DIR}/render/GLContextManager.cpp
    ${CORE_SRC_DIR}/stream/HLSStream.cpp
    ${CORE_SRC_DIR}/stream/StreamFactory.cpp
    ${DEMUXER_SRC_DIR}/M3U8Parser.cpp
    ${DEMUXER_SRC_DIR}/DemuxerManager.cpp
    ${CORE_SRC_DIR}/codec/DecoderManager.cpp
    ${CORE_SRC_DIR}/codec/RawDecoder.cpp
    ${DEMUXER_SRC_DIR}/Mp4Box.cpp
    ${DEMUXER_SRC_DIR}/Mp4Demuxer.cpp
    ${DEMUXER_SRC_DIR}/HLSDemuxer.cpp
    ${DEMUXER_SRC_DIR}/WavDemuxer.cpp
)

add_executable(${PROJECT_NAME} ${TEST_FILES} ${ENGINE_SOURCES} ${LEGACY_SOURCES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
    ${CORE_SRC_DIR}
    ${ENGINE_SRC_DIR}
    ${ENGINE_SRC_DIR}/source
    ${ENGINE_SRC_DIR}/pipeline
    ${CORE_SRC_DIR}/demuxer
    ${CORE_SRC_DIR}/codec
    ${CORE_SRC_DIR}/media
    ${CORE_SRC_DIR}/stream
    ${CORE_SRC_DIR}/render
    ${PERF_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/http
    ${CMAKE_CURRENT_SOURCE_DIR}/../../internal
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(${PROJECT_NAME}
    slark_base
    slark_perf
    slark_internal
    gtest_main
    gmock
    pthread)

if(NOT DISABLE_HTTP)
    target_link_libraries(${PROJECT_NAME} slark_http)
endif()

gtest_discover_tests(${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
    TIMEOUT 120
    LABELS "engine"
)
