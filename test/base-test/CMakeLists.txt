cmake_minimum_required(VERSION 3.20)

include_directories(${CMAKE_SOURCE_DIR}/slark/base ${CMAKE_SOURCE_DIR}/slark/third_party)

add_executable(base-test  ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${CMAKE_SOURCE_DIR}/slark/third_party/date.h)

target_link_libraries(base-test
        gtest
        gtest_main
        pthread
        slark_base)

add_test(
        NAME base
        COMMAND $<TARGET_FILE:base-test>
)

set_property(
        TEST base
        PROPERTY LABELS lib base
)