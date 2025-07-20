macro(add_framework FRAME_WORK_NAME TARGET_NAME)
    find_library(FRAMEWORK_${FRAME_WORK_NAME}
            NAMES ${FRAME_WORK_NAME}
            PATHS ${CMAKE_OSX_SYSROOT}/System/Library
            PATH_SUFFIXES Frameworks
            NO_DEFAULT_PATH)
    if( ${FRAMEWORK_${FRAME_WORK_NAME}} STREQUAL FRAMEWORK_${FRAME_WORK_NAME}-NOTFOUND)
        MESSAGE(ERROR ": Framework ${FRAME_WORK_NAME} not found")
    else()
        target_link_libraries(${TARGET_NAME} PUBLIC ${FRAMEWORK_${FRAME_WORK_NAME}})
        MESSAGE(STATUS "Framework ${FRAME_WORK_NAME} found at ${FRAMEWORK_${FRAME_WORK_NAME}}")
    endif()
endmacro(add_framework)

# iOS specific configurations
macro(configure_ios_target TARGET_NAME)
    set_target_properties(${TARGET_NAME} PROPERTIES
            FRAMEWORK TRUE
            FRAMEWORK_VERSION C
            VERSION 0.0.1
            SOVERSION 0.0.1
            MACOSX_FRAMEWORK_IDENTIFIER "com.slark.slarkFramework"
            PUBLIC_HEADER src/interface/iOS/*.h
            XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++23"
            XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
            XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Debug] "YES"
            XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Release] "NO"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
            XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
            )

    # Add iOS frameworks
    target_compile_definitions(${TARGET_NAME} PRIVATE GLES_SILENCE_DEPRECATION)
    target_compile_definitions(${TARGET_NAME} PUBLIC SLARK_IOS)
    add_framework(Foundation ${TARGET_NAME})
    add_framework(AVFoundation ${TARGET_NAME})
    add_framework(VideoToolBox ${TARGET_NAME})
    add_framework(AudioToolBox ${TARGET_NAME})
    add_framework(CoreMedia ${TARGET_NAME})
endmacro()

# iOS source files configuration
macro(add_ios_sources TARGET_NAME)
    set(iOS_SOURCE_PATTERNS
            src/platform/iOS/*.cpp
            src/platform/iOS/*.hpp
            src/platform/iOS/*.h
            src/platform/iOS/*.m
            src/platform/iOS/*.mm
            src/interface/iOS/*.h
            src/interface/iOS/*.m
            src/interface/iOS/*.mm
            )

    file(GLOB iOS_FILES ${iOS_SOURCE_PATTERNS})
    target_sources(${TARGET_NAME} PUBLIC ${iOS_FILES})

    # Add iOS-specific include directories
    target_include_directories(${TARGET_NAME} PUBLIC src/interface/iOS/)
    target_include_directories(${TARGET_NAME} PUBLIC src/platform/iOS/)
endmacro()