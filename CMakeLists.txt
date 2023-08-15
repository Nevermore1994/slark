cmake_minimum_required(VERSION 3.20)

enable_language(CXX)
enable_language(OBJC)

include(CMakeForceCompiler)
include(CMakePrintHelpers)

#C++ standard
set(CMAKE_CXX_FLAGS "-std=c++17 -Wall -Wextra -Wpedantic -Wcast-align -Wcast-qual -Wconversion -Wdisabled-optimization -Wendif-labels -Wfloat-equal -Winit-self -Winline -Wmissing-include-dirs -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wpacked -Wpointer-arith -Wredundant-decls -Wshadow -Wsign-promo -Wvariadic-macros -Wwrite-strings -Wno-variadic-macros")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -s -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -s -DNDEBUG -march=native")

message(STATUS "current path: " ${CMAKE_CURRENT_SOURCE_DIR})

set(COMPONENT_LIST "audio" "http")
set(DISABLE_TEST 0)

project(slark)

#set thread library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


#source file path
set(BASE_DIR slark/base)
set(THIRD_PARTY_DIR slark/third_party)
set(CORE_DIR slark/core)
set_property(GLOBAL PROPERTY LIBRARY_TYPE "base")
set(LINK_LIBRARY "" ${CMAKE_THREAD_LIBS_INIT})

function(add_base_library PLATFORM)
    message(STATUS "add base library")
    set(BASE_FILE_LISTS ${BASE_DIR}/*.cpp ${BASE_DIR}/*.hpp ${BASE_DIR}/*.h ${THIRD_PARTY_DIR}/*.h)
    set(BASE_HEADER "")
    if(${PLATFORM} STREQUAL "iOS")
        list(APPEND BASE_FILE_LISTS
        ${BASE_DIR}/platform/iOS/*.cpp 
        ${BASE_DIR}/platform/iOS/*.hpp 
        ${BASE_DIR}/platform/iOS/*.h 
        ${BASE_DIR}/platform/iOS/*.m 
        ${BASE_DIR}/platform/iOS/*.mm
        )
        set(BASE_HEADER ${BASE_DIR}/platform/iOS)
        message(${BASE_HEADER})
    endif()
    FILE(GLOB BASE_FILES ${BASE_FILE_LISTS})
    add_library(base ${BASE_FILES})
    target_include_directories(base PUBLIC ${BASE_DIR})
    target_include_directories(base PUBLIC ${THIRD_PARTY_DIR})
    target_include_directories(base PUBLIC ${BASE_HEADER})
endfunction()

function(add_core_library)
    message(STATUS "add core library")
    FILE(GLOB CORE_FILES ${CORE_DIR}/*.cpp ${CORE_DIR}/*.hpp ${CORE_DIR}/*.h)
    target_sources(${PROJECT_NAME} PUBLIC ${CORE_FILES})
    target_include_directories(${PROJECT_NAME} PUBLIC ${CORE_DIR})
    source_group("core" FILES ${CORE_FILES})
endfunction()

function(add_component NAME)
    message(STATUS "add ${NAME} library")
    FILE(GLOB FILES ${CORE_DIR}/${NAME}/*.cpp ${CORE_DIR}/${NAME}/*.hpp ${CORE_DIR}/${NAME}/*.h)
    target_sources(${PROJECT_NAME} PUBLIC ${FILES})
    target_include_directories(${PROJECT_NAME} PUBLIC ${CORE_DIR}/${NAME})
    source_group("core/${NAME}" FILES ${FILES})
endfunction()


macro(add_framework fwname appname)
    find_library(FRAMEWORK_${fwname}
    NAMES ${fwname}
    PATHS ${CMAKE_OSX_SYSROOT}/System/Library
    PATH_SUFFIXES Frameworks
    NO_DEFAULT_PATH)
    if( ${FRAMEWORK_${fwname}} STREQUAL FRAMEWORK_${fwname}-NOTFOUND)
        MESSAGE(ERROR ": Framework ${fwname} not found")
    else()
        target_link_libraries(${appname} "${FRAMEWORK_${fwname}}")
        MESSAGE(STATUS "Framework ${fwname} found at ${FRAMEWORK_${fwname}}")
    endif()
endmacro(add_framework)


function(add_ios_library)
    message(STATUS "add iOS library")

    FILE(GLOB PLATFORM_CORE_FILE 
        ${CORE_DIR}/platform/iOS/*.cpp 
        ${CORE_DIR}/platform/iOS/*.hpp 
        ${CORE_DIR}/platform/iOS/*.h 
        ${CORE_DIR}/platform/iOS/*.m 
        ${CORE_DIR}/platform/iOS/*.mm
    )

    target_sources(${PROJECT_NAME} PUBLIC ${PLATFORM_CORE_FILE})
    target_include_directories(${PROJECT_NAME} PUBLIC ${CORE_DIR}/platform/iOS)
    source_group("core/iOS" FILES ${PLATFORM_CORE_FILE})

    add_framework(Foundation ${PROJECT_NAME})
    add_framework(AVFoundation ${PROJECT_NAME})
    add_framework(VideoToolBox ${PROJECT_NAME})
    add_framework(AudioToolBox ${PROJECT_NAME})

    set_target_properties(${PROJECT_NAME} PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION C
        # "current version" in semantic format in Mach-O binary file
        VERSION 16.4.0
        # "compatibility version" in semantic format in Mach-O binary file
        SOVERSION 1.0.0
        PUBLIC_HEADER ${PROJECT_NAME}Framework.h
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Debug] "YES"
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Release] "NO"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
    )
    set(FRAMEWORK_BUNDLE_IDENTIFIER "com.slark.slarkframework")  
    set(FRAMEWORK_NAME "slark")  
endfunction()

function(add_android_library)
    message(STATUS "add android library")
endfunction()



function(set_platform PLATFORM)
    message(STATUS "platform:${PLATFORM}")
    if(${PLATFORM} STREQUAL "iOS")
        add_definitions(-DSLARK_IOS) #set SLARK_IOS = 1
    elseif(${PLATFORM} STREQUAL "Android")
        add_definitions(-DSLARK_ANDROID) #set SLARK_ANDROID = 1
    endif()
endfunction()


function(set_build_type TYPE)
    message(STATUS "build:${TYPE}")
    if(${TYPE} STREQUAL "Debug")
        set(CMAKE_BUILD_TYPE DEBUG PARENT_SCOPE)
        add_definitions(-DDEBUG) #set DEBUG = 1
    else()
        set(CMAKE_BUILD_TYPE RELEASE PARENT_SCOPE)
    endif()
endfunction()

function(set_output_type TYPE)
    message(STATUS "output:${TYPE}")
    if(${TYPE} STREQUAL "exe")
        add_executable(${PROJECT_NAME} "")
    else()
        add_library(${PROJECT_NAME} SHARED "")
    endif()
endfunction()

function(set_main_component COMPONENT_NAME PLATFORM)
    message(STATUS "main component:${COMPONENT_NAME}")
    #must add base library
    add_base_library(${PLATFORM})
    
    #add core library
    if(${COMPONENT_NAME} STREQUAL "core")
        add_core_library()
        add_component("codec")
        add_component("muxer")
        set_property(GLOBAL PROPERTY LIBRARY_TYPE "core")
    endif()
    
endfunction()

function(load_component COMPONENT_NAME_LIST)
    message(STATUS "load component:" ${COMPONENT_NAME_LIST})
    foreach(name IN LISTS COMPONENT_NAME_LIST)
        add_component(${name})
    endforeach ()
endfunction()


function(read_config)
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/script/config.json CONFIG_JSON_STRING)
    #parse platform
    string(JSON PLATFORM GET ${CONFIG_JSON_STRING} platform)
    set_platform(${PLATFORM})
    
    #build_type 
    string(JSON BUILD_TYPE GET ${CONFIG_JSON_STRING} build_type)
    set_build_type(${BUILD_TYPE})
     
    #parse output
    string(JSON OUTPUT_TYPE GET ${CONFIG_JSON_STRING} output)
    if(${PLATFORM} STREQUAL "iOS" OR ${PLATFORM} STREQUAL "Android")
        set_output_type("lib")
    else()
        set_output_type(${OUTPUT_TYPE})
    endif()

    
    #parse component
    string(JSON COMPONENT_TYPE GET ${CONFIG_JSON_STRING} component main)
    set_main_component(${COMPONENT_TYPE} ${PLATFORM})
    
    #parse disable
    if(${COMPONENT_TYPE} STREQUAL "core")
        string(JSON DISABLE_COMPONENT_LENGTH LENGTH ${CONFIG_JSON_STRING} component disable)
        foreach (i RANGE 0 ${DISABLE_COMPONENT_LENGTH} )
            if(${i} EQUAL ${DISABLE_COMPONENT_LENGTH})
                continue()
            endif()
            string(JSON DISABLE_COMPONENT_NAME GET ${CONFIG_JSON_STRING} component disable ${i})
            message(STATUS "remove:" ${DISABLE_COMPONENT_NAME})
            list(REMOVE_ITEM COMPONENT_LIST ${DISABLE_COMPONENT_NAME})
        endforeach()
        list(LENGTH COMPONENT_LIST COMPONENT_LIST_LENGTH)
        if(${COMPONENT_LIST_LENGTH} GREATER 0)
            load_component(${COMPONENT_LIST})
        endif ()
    endif ()

    if(${PLATFORM} STREQUAL "iOS")
        add_ios_library()
    elseif(${PLATFORM} STREQUAL "Android")
    endif()
    
    #add test main
    if(${OUTPUT_TYPE} STREQUAL "exe")
        if (${COMPONENT_TYPE} STREQUAL "base")
            target_sources(${PROJECT_NAME} PUBLIC slark/baseMain.cpp)
            source_group("SourceFiles" FILES slark/baseMain.cpp)
        else()
            target_sources(${PROJECT_NAME} PUBLIC slark/main.cpp)
            source_group("SourceFiles" FILES slark/main.cpp)
        endif()
    endif()

    #enable test
    if(${PLATFORM} STREQUAL "iOS" OR ${PLATFORM} STREQUAL "Android")
        set(DISABLE_TEST 1 PARENT_SCOPE)
    else()
        string(JSON DISABLE_TEST_VALUE GET ${CONFIG_JSON_STRING} disable_test)
        set(DISABLE_TEST ${DISABLE_TEST_VALUE} PARENT_SCOPE)
    endif()
endfunction()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build")

read_config()

#ctest only running main flow, don't move to function
if(${DISABLE_TEST} EQUAL 0)
    message(STATUS "enable testing")
    execute_process(COMMAND rm -rf ${CMAKE_CURRENT_SOURCE_DIR}/_deps)
    execute_process(COMMAND rm -rf ${CMAKE_CURRENT_SOURCE_DIR}/script/_deps)
    enable_testing()
    include(cmake/gtest.cmake)
    add_subdirectory(test)
else()
    message(STATUS "disable testing")
endif()

target_link_libraries(${PROJECT_NAME} base ${CMAKE_THREAD_LIBS_INIT} )
