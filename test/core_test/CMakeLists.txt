cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

add_executable(core_test ${CMAKE_CURRENT_SOURCE_DIR}/test.cpp)

target_link_libraries(core_test
        gtest
        gtest_main
        pthread
        base
        )

gtest_add_tests(TARGET core_test)
