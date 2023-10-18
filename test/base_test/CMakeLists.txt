cmake_minimum_required(VERSION 3.20)

include(GoogleTest)
#include_directories(${CMAKE_SOURCE_DIR}/slark/base ${CMAKE_SOURCE_DIR}/slark/third_party)

add_executable(base_test
            ${CMAKE_CURRENT_SOURCE_DIR}/CTimeTest.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/StringUtilTest.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/SafeDequeTest.cpp
            )
            
set_target_properties(base_test PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION C
        XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++23"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Debug] "YES"
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Release] "NO"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
        XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
        MACOSX_FRAMEWORK_IDENTIFIER "com.slark.basetest"
)

target_link_libraries(base_test
        gtest
        gtest_main
        pthread
        base)

gtest_add_tests(TARGET base_test)

