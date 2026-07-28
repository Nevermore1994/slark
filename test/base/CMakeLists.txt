cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

PROJECT(Base_test)

set(TEST_FILE_LISTS
        *.cpp
)
file(GLOB TEST_FILES ${TEST_FILE_LISTS})

add_executable(${PROJECT_NAME} ${TEST_FILES})

target_link_libraries(${PROJECT_NAME}
        slark_base
        gtest_main
        pthread)

gtest_discover_tests(${PROJECT_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PROPERTIES
        TIMEOUT 300
        PARALLEL_LEVEL 0
        LABELS "base"
)

add_custom_target(run_base_test_parallel
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j$(nproc) -L base
        DEPENDS ${PROJECT_NAME}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running base tests in parallel"
)
