cmake_minimum_required(VERSION 3.20)

set(BASE_FILE_LISTS *.cpp *.hpp *.h)
set(BASE_HEADER "")


file(GLOB BASE_FILES ${BASE_FILE_LISTS})
add_library(base ${BASE_FILES})

if(${BUILD_PLATFORM} STREQUAL "iOS")
    set(BASE_iOS_FILE_LISTS
        platform/iOS/*.cpp
        platform/iOS/*.hpp
        platform/iOS/*.h
        platform/iOS/*.m
        platform/iOS/*.mm
        )
    file(GLOB BASE_iOS_FILES ${BASE_iOS_FILE_LISTS})
    add_framework(Foundation base)
    message(${BASE_iOS_FILES})
    target_sources(base PUBLIC ${BASE_iOS_FILES})
    target_include_directories(base PUBLIC ./platform/iOS)
elseif (${BUILD_PLATFORM} STREQUAL "Android")

endif()

if(DISABLE_TEST)
    target_include_directories(base PRIVATE .)
else()
    target_include_directories(base PUBLIC .)
endif ()