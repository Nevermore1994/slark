cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

PROJECT(Base_test)

set(TEST_FILE_LISTS *.cpp ../../src/base/*.cpp ../../src/base/*.hpp ../../src/base/*.h)
file(GLOB TEST_FILES ${TEST_FILE_LISTS})
add_executable(${PROJECT_NAME} ${TEST_FILES})

target_link_libraries(${PROJECT_NAME}
        gtest
        gtest_main
        pthread)
target_include_directories(${PROJECT_NAME} PRIVATE ../../src/base)

gtest_add_tests(TARGET ${PROJECT_NAME})

