cmake_minimum_required(VERSION 3.20)

project(slark)

#C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
#
## Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Wcast-align -Wcast-qual -Wconversion -Wdisabled-optimization -Wendif-labels -Wfloat-equal -Winit-self -Winline -Wmissing-include-dirs -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wpacked -Wpointer-arith -Wredundant-decls -Wshadow -Wsign-promo -Wwrite-strings -Wno-variadic-macros -Wno-unknown-pragmas -Wno-undef")

# Set thread library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# print current path
message(STATUS "current path: " ${CMAKE_CURRENT_SOURCE_DIR})

# add core library
set(BUILD_PLATFORM "PC" CACHE STRING "build platform")
option(DISABLE_TEST "disable test" OFF)
option(DISABLE_HTTP "disable http" OFF)

macro(read_config)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/config.json CONFIG_JSON_STRING)

    #parse build platform
    string(JSON BUILD_PLATFORM_VALUE GET ${CONFIG_JSON_STRING} platform)
    set(BUILD_PLATFORM ${BUILD_PLATFORM_VALUE} CACHE STRING "build platform" FORCE)

    #parse and set build type
    string(JSON BUILD_TYPE GET ${CONFIG_JSON_STRING} build_type)
    message(STATUS "build:${BUILD_TYPE}")
    if(${BUILD_TYPE} STREQUAL "Debug")
        set(CMAKE_BUILD_TYPE DEBUG)
        add_definitions(-DDEBUG) #set DEBUG = 1
    else()
        set(CMAKE_BUILD_TYPE RELEASE)
        add_definitions(-DRELEASE) #set RELEASE = 1
    endif()

    if(${BUILD_PLATFORM_VALUE} STREQUAL "iOS" OR ${BUILD_PLATFORM_VALUE} STREQUAL "Android")
        set(DISABLE_TEST ON CACHE BOOL "Disable test" FORCE)
    else()
        string(JSON DISABLE_TEST_VALUE GET ${CONFIG_JSON_STRING} disable_test)
        set(DISABLE_TEST ${DISABLE_TEST_VALUE} CACHE BOOL "Disable test" FORCE)
    endif()

    string(JSON DISABLE_HTTP_VALUE GET ${CONFIG_JSON_STRING} disable_http)
    set(DISABLE_HTTP ${DISABLE_HTTP_VALUE} CACHE BOOL "Disable http" FORCE)

endmacro()

#add base file
macro(add_base_file TARGET_NAME)
    set(BASE_FILE_LISTS src/base/*.cpp src/base/*.hpp src/base/*.h)
    file(GLOB BASE_FILES ${BASE_FILE_LISTS})

    target_sources(${TARGET_NAME} PUBLIC ${BASE_FILES})
    target_include_directories(${TARGET_NAME} PUBLIC src/base)
endmacro()

macro(add_core_file TARGET_NAME)
    set(CORE_FILE_LISTS src/core/*.cpp src/core/*.hpp src/core/*.h
            src/core/audio/*.cpp src/core/audio/*.hpp src/core/audio/*.h
            src/core/muxer/*.cpp src/core/muxer/*.hpp src/core/muxer/*.h
            src/core/codec/*.cpp src/core/codec/*.hpp src/core/codec/*.h
            src/core/video/*.h src/core/public/*.cpp public/*.h
            )
    file(GLOB CORE_FILES ${CORE_FILE_LISTS})
    target_sources(${TARGET_NAME} PUBLIC ${CORE_FILES})

    target_include_directories(${TARGET_NAME} PUBLIC src/core/public)
    target_include_directories(${TARGET_NAME} PUBLIC src/core
        src/core/muxer
        src/core/audio
        src/core/codec
        src/core/video
        src/core/opengles
    )
endmacro()

read_config()
add_library(${PROJECT_NAME} SHARED "")

#add base file
add_base_file(${PROJECT_NAME})
#add core file
add_core_file(${PROJECT_NAME})

if(${BUILD_PLATFORM} STREQUAL "iOS")
    message(STATUS "add iOS interface")
    include(CMake-ios.cmake)
    add_ios_sources(${PROJECT_NAME})
    configure_ios_target(${PROJECT_NAME})
elseif (${BUILD_PLATFORM} STREQUAL "Android")
    include(cmake/android.cmake)
    message(STATUS "add Android interface")
    target_link_libraries(${PROJECT_NAME} PUBLIC Android_INTERFACE)
endif ()

if(NOT DISABLE_HTTP)
    message(STATUS "enable http")
    add_subdirectory(src/http)
    target_link_libraries(${PROJECT_NAME} PUBLIC http_request)
else()
    message(STATUS "disable http")
endif()


if(NOT DISABLE_TEST)
    message(STATUS "enable testing")
    enable_testing()
    include(cmake/gtest.cmake)
    add_subdirectory(test)
else()
    message(STATUS "disable testing")
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC ${CMAKE_THREAD_LIBS_INIT})

