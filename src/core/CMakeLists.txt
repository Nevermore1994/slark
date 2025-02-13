cmake_minimum_required(VERSION 3.20)

project(core)

set(FILE_LISTS *.cpp *.hpp *.h
        audio/*.cpp audio/*.hpp audio/*.h
        muxer/*.cpp muxer/*.hpp muxer/*.h
        codec/*.cpp codec/*.hpp codec/*.h
        video/*.h public/*.cpp public/*.h
)

FILE(GLOB CORE_FILES ${FILE_LISTS})

add_library(${PROJECT_NAME} ${CORE_FILES})

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
    target_sources(${PROJECT_NAME} PUBLIC ${CORE_iOS_FILES})

    add_framework(AVFoundation ${PROJECT_NAME})
    add_framework(VideoToolBox ${PROJECT_NAME})
    add_framework(AudioToolBox ${PROJECT_NAME})
    add_framework(CoreMedia ${PROJECT_NAME})
    target_include_directories(${PROJECT_NAME} PUBLIC ./platform/iOS)
    target_include_directories(${PROJECT_NAME} PUBLIC ../base/platform/iOS)

elseif (${BUILD_PLATFORM} STREQUAL "Android")

endif()

target_include_directories(${PROJECT_NAME} PUBLIC public)
target_include_directories(${PROJECT_NAME} PUBLIC ../base)
target_include_directories(${PROJECT_NAME} PUBLIC ../http/include/public)
target_include_directories(${PROJECT_NAME} PUBLIC . muxer audio codec video opengles)
target_compile_definitions(${PROJECT_NAME} PRIVATE GLES_SILENCE_DEPRECATION)