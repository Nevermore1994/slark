cmake_minimum_required(VERSION 3.20)

include(GoogleTest)
include_directories(${CMAKE_SOURCE_DIR}/slark/base ${CMAKE_SOURCE_DIR}/slark/third_party)

add_executable(base_test  ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${CMAKE_SOURCE_DIR}/slark/third_party/date.h)

target_link_libraries(base_test
        gtest
        gtest_main
        pthread
        slark_base)

#add_test(
#        NAME base_test
#        COMMAND $<TARGET_FILE:base-test>
#)

#set_property(
#        TEST base_test
#        PROPERTY LABELS lib base
#)

gtest_add_tests(TARGET base_test)

