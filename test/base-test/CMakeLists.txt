cmake_minimum_required(VERSION 3.20)

add_executable(base-test main.cpp)
target_link_libraries(base-test
        gtest
        gtest_main
        pthread
        )

add_test(
        NAME base
        COMMAND $<TARGET_FILE:base-test>
)

set_property(
        TEST base
        PROPERTY LABELS lib base
)