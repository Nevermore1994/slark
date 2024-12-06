cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

set(TEST_FILE_LISTS *.cpp)
file(GLOB TEST_FILES ${TEST_FILE_LISTS})
add_executable(http_test ${TEST_FILES})

message("http test")
target_link_libraries(http_test
        gtest
        gtest_main
        pthread
        http_request
        base)

gtest_add_tests(TARGET http_test)
