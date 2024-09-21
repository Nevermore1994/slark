cmake_minimum_required(VERSION 3.20)

set(FILE_LISTS *.cpp *.hpp *.h
        audio/*.cpp audio/*.hpp audio/*.h
        muxer/*.cpp muxer/*.hpp muxer/*.h
        codec/*.cpp codec/*.hpp codec/*.h
        public/*.cpp public/*.h
)

FILE(GLOB CORE_FILES ${FILE_LISTS})

add_library(core ${CORE_FILES})

if(${BUILD_PLATFORM} STREQUAL "iOS")
    SET(CORE_iOS_FILES_LISTS
            platform/iOS/*.cpp
            platform/iOS/*.hpp
            platform/iOS/*.h
            platform/iOS/*.m
            platform/iOS/*.mm
            interface/iOS/*.h
            interface/iOS/*.m
            interface/iOS/*.mm
            )
    file(GLOB CORE_iOS_FILES ${CORE_iOS_FILES_LISTS})
    target_sources(core PUBLIC ${CORE_iOS_FILES})

    add_framework(AVFoundation core)
    add_framework(VideoToolBox core)
    add_framework(AudioToolBox core)
    target_include_directories(core PUBLIC ./platform/iOS)
    target_include_directories(core PUBLIC ../base/platform/iOS)

    set_target_properties(${PROJECT_NAME} PROPERTIES
            FRAMEWORK TRUE
            FRAMEWORK_VERSION C
            # "current version" in semantic format in Mach-O binary file
            VERSION 0.0.1
            # "compatibility version" in semantic format in Mach-O binary file
            SOVERSION 0.0.1
            MACOSX_FRAMEWORK_IDENTIFIER "com.slark.slarkFramework"
            PUBLIC_HEADER ${PROJECT_NAME}Framework.h
            XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++23"
            XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
            XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Debug] "YES"
            XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Release] "NO"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
            XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
            )
elseif (${BUILD_PLATFORM} STREQUAL "Android")

endif()

target_include_directories(core PUBLIC public interface)
target_include_directories(core PRIVATE . muxer audio codec)
target_include_directories(core PRIVATE ../base)