cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

set(TEST_FILE_LISTS *.cpp)
file(GLOB TEST_FILES ${TEST_FILE_LISTS})
add_executable(base_test ${TEST_FILES} )

target_link_libraries(base_test
        gtest
        gtest_main
        pthread
        base)

gtest_add_tests(TARGET base_test)

