cmake_minimum_required(VERSION 3.20)

include(GoogleTest)

set(TEST_FILE_LISTS *.cpp)
file(GLOB TEST_FILES ${TEST_FILE_LISTS})
add_executable(base_test ${TEST_FILES} )

if(${BUILD_PLATFORM} STREQUAL "iOS")
set_target_properties(base_test PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION C
        XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++23"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Debug] "YES"
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Release] "NO"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
        XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
        MACOSX_FRAMEWORK_IDENTIFIER "com.slark.baseTest"
)

endif ()

target_link_libraries(base_test
        gtest
        gtest_main
        pthread
        base)

gtest_add_tests(TARGET base_test)

