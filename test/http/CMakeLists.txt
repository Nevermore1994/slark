cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

file(GLOB HTTP_TEST_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
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
