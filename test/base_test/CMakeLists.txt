cmake_minimum_required(VERSION 3.20)

include(GoogleTest)
#include_directories(${CMAKE_SOURCE_DIR}/slark/base ${CMAKE_SOURCE_DIR}/slark/third_party)

add_executable(base_test ${CMAKE_CURRENT_SOURCE_DIR}/CTimeTest.cpp  ${CMAKE_CURRENT_SOURCE_DIR}/stringTest.cpp)

target_link_libraries(base_test
        gtest
        gtest_main
        pthread
        base)

gtest_add_tests(TARGET base_test)

