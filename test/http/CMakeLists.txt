cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

set(HTTP_TEST_FILES
        HttpModuleTest.cpp
        HttpAsyncRequestTest.cpp
        RequestTest.cpp
        UrlParseTest.cpp
)

add_executable(http_test ${HTTP_TEST_FILES})

target_link_libraries(http_test
        slark_base
        slark_http
        gtest_main
        gmock_main
        pthread)

gtest_discover_tests(http_test
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PROPERTIES
        TIMEOUT 300
        LABELS "http"
)
