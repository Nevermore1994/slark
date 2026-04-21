cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

PROJECT(Base_test)

set(TEST_FILE_LISTS
        *.cpp
        AsyncGeneratorTest.cpp
        PlayerStateMachineTest.cpp
)
file(GLOB TEST_FILES ${TEST_FILE_LISTS})
add_executable(${PROJECT_NAME} ${TEST_FILES})

target_link_libraries(${PROJECT_NAME}
        slark_base
        slark_core
        gtest_main
        pthread)

# 使用 gtest_discover_tests 支持并行测试发现和 CTest 并行执行
gtest_discover_tests(${PROJECT_NAME}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        PROPERTIES

        # 设置测试超时（秒）
        TIMEOUT 300

        # 允许并行运行
        PARALLEL_LEVEL 0

        # 失败时显示详细输出
        LABELS "base"
)

# 定义自定义目标用于并行运行测试
add_custom_target(run_base_test_parallel
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -j$(nproc) -L base
        DEPENDS ${PROJECT_NAME}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running base tests in parallel"
)
