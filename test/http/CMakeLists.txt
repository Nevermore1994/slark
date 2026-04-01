cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

set(TEST_FILE_LISTS *.cpp)
file(GLOB TEST_FILES ${TEST_FILE_LISTS})
add_executable(http_test ${TEST_FILES})
target_link_libraries(http_test
        slark_base
        gtest
        gtest_main
        pthread
        http_request
        slark)

gtest_add_tests(TARGET http_test)
