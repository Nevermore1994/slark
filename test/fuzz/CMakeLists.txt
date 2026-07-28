cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

set(BASE_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/base)
set(CORE_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/core)
set(DEMUXER_SRC_DIR ${CORE_SRC_DIR}/demuxer)
set(HTTP_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/http)



# =============================================================================
# CoreFuzz_test — M3U8 解析器 + Mp4Box + Mp4Demuxer + WAV + TS + MediaUtil 随机测试
# =============================================================================
# 复用 core test 的源文件依赖
set(CORE_FUZZ_SOURCES
    ${CORE_SRC_DIR}/PlayerStateMachine.cpp
    ${CORE_SRC_DIR}/MediaDefs.cpp
    ${CORE_SRC_DIR}/MediaUtil.cpp
    ${CORE_SRC_DIR}/MediaPlaylist.cpp
    ${CORE_SRC_DIR}/NotifyCenter.cpp
    ${CORE_SRC_DIR}/Event.cpp
    ${CORE_SRC_DIR}/pipeline/BufferMonitor.cpp
    # ${CORE_SRC_DIR}/perf/PlaySession.cpp
    ${DEMUXER_SRC_DIR}/M3U8Parser.cpp
    ${DEMUXER_SRC_DIR}/DemuxerManager.cpp
    ${DEMUXER_SRC_DIR}/Mp4Box.cpp
    ${DEMUXER_SRC_DIR}/Mp4Demuxer.cpp
    ${DEMUXER_SRC_DIR}/HLSDemuxer.cpp
    ${DEMUXER_SRC_DIR}/WavDemuxer.cpp
)

add_executable(CoreFuzz_test
    ${CMAKE_CURRENT_SOURCE_DIR}/M3U8ParserFuzzTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Mp4BoxFuzzTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Mp4DemuxerFuzzTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/WavDemuxerFuzzTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TSDemuxerFuzzTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MediaUtilFuzzTest.cpp
    ${CORE_FUZZ_SOURCES}
)
target_include_directories(CoreFuzz_test PRIVATE
    ${CORE_SRC_DIR}
    ${CORE_SRC_DIR}/demuxer
    ${CORE_SRC_DIR}/pipeline
    ${CORE_SRC_DIR}/audio
    ${CORE_SRC_DIR}/codec
    ${CORE_SRC_DIR}/perf
    ${HTTP_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../internal
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(CoreFuzz_test
    slark_base
    slark_internal
    gtest_main
    gmock
    pthread)
if(NOT DISABLE_HTTP)
    target_link_libraries(CoreFuzz_test slark_http)
endif()
gtest_discover_tests(CoreFuzz_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES
    TIMEOUT 300
    LABELS "fuzz"
)

# =============================================================================
# HttpFuzz_test — HTTP 响应解析器 + HttpUtil + URL + HttpProcessor 随机测试
# =============================================================================
add_executable(HttpFuzz_test
    ${CMAKE_CURRENT_SOURCE_DIR}/HttpResponseParserFuzzTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HttpUtilFuzzTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UrlFuzzTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HttpProcessorFuzzTest.cpp
)
target_include_directories(HttpFuzz_test PRIVATE
    ${HTTP_SRC_DIR}
    ${BASE_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(HttpFuzz_test
    slark_base
    slark_http
    gtest_main
    gmock_main
    pthread)
gtest_discover_tests(HttpFuzz_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES
    TIMEOUT 300
    LABELS "fuzz"
)

# =============================================================================
# StringUtilFuzz_test — 字符串工具随机测试
# =============================================================================
add_executable(StringUtilFuzz_test
    ${CMAKE_CURRENT_SOURCE_DIR}/StringUtilFuzzTest.cpp
)
target_include_directories(StringUtilFuzz_test PRIVATE
    ${BASE_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(StringUtilFuzz_test
    slark_base
    gtest_main
    pthread)
gtest_discover_tests(StringUtilFuzz_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES
    TIMEOUT 300
    LABELS "fuzz"
)

# =============================================================================
# SeekContextFuzz_test — 多线程 seek 状态机竞争测试
# =============================================================================
add_executable(SeekContextFuzz_test
    ${CMAKE_CURRENT_SOURCE_DIR}/SeekContextFuzzTest.cpp
)
target_include_directories(SeekContextFuzz_test PRIVATE
    ${CORE_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(SeekContextFuzz_test
    slark_base
    gtest_main
    pthread)
gtest_discover_tests(SeekContextFuzz_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES
    TIMEOUT 300
    LABELS "fuzz"
)

# =============================================================================
# TimerPoolFuzz_test — 时间轮随机添加/取消/过期竞争测试
# =============================================================================
add_executable(TimerPoolFuzz_test
    ${CMAKE_CURRENT_SOURCE_DIR}/TimerPoolFuzzTest.cpp
)
target_include_directories(TimerPoolFuzz_test PRIVATE
    ${BASE_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(TimerPoolFuzz_test
    slark_base
    gtest_main
    pthread)
gtest_discover_tests(TimerPoolFuzz_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES
    TIMEOUT 300
    LABELS "fuzz"
)

# =============================================================================
# FileStreamFuzz_test — 文件流并发 read/seek/pause 状态机测试
# =============================================================================
add_executable(FileStreamFuzz_test
    ${CMAKE_CURRENT_SOURCE_DIR}/FileStreamFuzzTest.cpp
)
target_include_directories(FileStreamFuzz_test PRIVATE
    ${BASE_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(FileStreamFuzz_test
    slark_base
    gtest_main
    pthread)
gtest_discover_tests(FileStreamFuzz_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES
    TIMEOUT 300
    LABELS "fuzz"
)

# =============================================================================
# WriterFuzz_test — 并发写入/关闭/停止状态机测试
# =============================================================================
add_executable(WriterFuzz_test
    ${CMAKE_CURRENT_SOURCE_DIR}/WriterFuzzTest.cpp
)
target_include_directories(WriterFuzz_test PRIVATE
    ${BASE_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(WriterFuzz_test
    slark_base
    gtest_main
    pthread)
gtest_discover_tests(WriterFuzz_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES
    TIMEOUT 300
    LABELS "fuzz"
)

# =============================================================================
# ThreadFuzz_test — 线程并发 task/post/stop 竞争测试
# =============================================================================
add_executable(ThreadFuzz_test
    ${CMAKE_CURRENT_SOURCE_DIR}/ThreadFuzzTest.cpp
)
target_include_directories(ThreadFuzz_test PRIVATE
    ${BASE_SRC_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(ThreadFuzz_test
    slark_base
    gtest_main
    pthread)
gtest_discover_tests(ThreadFuzz_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES
    TIMEOUT 300
    LABELS "fuzz"
)
