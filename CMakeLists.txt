cmake_minimum_required(VERSION 3.20)

include(CMakeForceCompiler)
include(CMakePrintHelpers)
include(FetchContent)

#C++ standard
set(CMAKE_CXX_FLAGS "-std=c++17 -Wall -Wextra -Wpedantic -Wcast-align -Wcast-qual -Wconversion -Wdisabled-optimization -Wendif-labels -Wfloat-equal -Winit-self -Winline -Wmissing-include-dirs -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wpacked -Wpointer-arith -Wredundant-decls -Wshadow -Wsign-promo -Wvariadic-macros -Wwrite-strings -Wno-variadic-macros")
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -pg")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -s -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-g3 -Og -pg")
set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -s -DNDEBUG -march=native")

message(STATUS "current path: " ${CMAKE_CURRENT_SOURCE_DIR})

set(COMPONENT_LIST "audio" "http")

project(slark)

#set thread library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

#source file path
set(BASE_DIR slark/base/)
set(THIRD_PARTY_DIR slark/third_party/)
set(CORE_DIR slark/core/)


function(add_base_library)
    message(STATUS "add base library")
    FILE(GLOB BASE_FILES ${BASE_DIR}/*.cpp ${BASE_DIR}/*.hpp ${BASE_DIR}/*.h ${THIRD_PARTY_DIR}/*.h)
    target_sources(${PROJECT_NAME} PUBLIC ${BASE_FILES})
    target_include_directories(${PROJECT_NAME} PUBLIC ${BASE_DIR})
    target_include_directories(${PROJECT_NAME} PUBLIC ${THIRD_PARTY_DIR})
    source_group("base" FILES ${BASE_FILES})
endfunction()

function(add_core_library)
    message(STATUS "add core library")
    FILE(GLOB CORE_FILES ${CORE_DIR}/*.cpp ${CORE_DIR}/*.hpp ${CORE_DIR}/*.h)
    target_sources(${PROJECT_NAME} PUBLIC ${CORE_FILES})
    target_include_directories(${PROJECT_NAME} PUBLIC ${CORE_DIR})
    source_group("core" FILES ${CORE_FILES})
endfunction()

function(add_component name)
    message(STATUS "add ${name} library")
    FILE(GLOB FILES ${CORE_DIR}/${name}/*.cpp ${CORE_DIR}/${name}/*.hpp ${CORE_DIR}/${name}/*.h)
    target_sources(${PROJECT_NAME} PUBLIC ${FILES})
    target_include_directories(${PROJECT_NAME} PUBLIC ${CORE_DIR}/${name})
    source_group("core/${name}" FILES ${FILES})
endfunction()


function(add_ios_library)
    message(STATUS "add ios library")
endfunction()

function(add_android_library)
    message(STATUS "add android library")
endfunction()

function(set_platform platform)
    message(STATUS "platform:${platform}")
    if(${PLATFORM} STREQUAL "iOS")
        add_definitions(-DSLARK_IOS) #set SLARK_IOS = 1
    elseif(${PLATFORM} STREQUAL "Android")
        add_definitions(-DSLARK_Android) #set SLARK_Android = 1
    endif()
endfunction()


function(set_build_type type)
    message(STATUS "build:${type}")
    if(${type} STREQUAL "Debug")
        set(CMAKE_BUILD_TYPE DEBUG PARENT_SCOPE)
        add_definitions(-DDEBUG) #set DEBUG = 1
    else()
        set(CMAKE_BUILD_TYPE RELEASE PARENT_SCOPE)
    endif()
endfunction()

function(set_output_type type)
    message(STATUS "output:${type}")
    if(${type} STREQUAL "exe")
        add_executable(${PROJECT_NAME} "")
    else()
        add_library(${PROJECT_NAME} "")
    endif()
endfunction()

function(set_main_component component_name)
    message(STATUS "main component:${component_name}")
    #must add base library
    add_base_library()
    
    #add core library
    if(${component_name} STREQUAL "core")
        add_core_library()
        add_component("codec")
        add_component("muxer")
    endif()
    
endfunction()

function(load_component component_name_list)
    message(STATUS "load component:" ${component_name_list})
    foreach(name IN LISTS component_name_list)
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
    set_output_type(${OUTPUT_TYPE})
    
    #parse component
    string(JSON MAIN_COMPONENT GET ${CONFIG_JSON_STRING} component main)
    set_main_component(${MAIN_COMPONENT})
    
    #parse disable
    if(${MAIN_COMPONENT} STREQUAL "core")
        string(JSON DISABLE_COMPONENT_LENGTH LENGTH ${CONFIG_JSON_STRING} component disable)
        foreach (i RANGE 0 ${DISABLE_COMPONENT_LENGTH})
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
    
    #add test main
    if(${OUTPUT_TYPE} STREQUAL "exe")
        if (${MAIN_COMPONENT} STREQUAL "base")
            target_sources(${PROJECT_NAME} PUBLIC slark/baseMain.cpp)
            source_group("SourceFiles" FILES slark/baseMain.cpp)
        else()
            target_sources(${PROJECT_NAME} PUBLIC slark/main.cpp)
            source_group("SourceFiles" FILES slark/main.cpp)
        endif()
    endif()
    
    #enable test
    string(JSON DISABLE_TEST GET ${CONFIG_JSON_STRING} disable_test)
    if(${DISABLE_TEST} EQUAL 0)
        message(STATUS "enable testing")
        execute_process(COMMAND rm -rf ${CMAKE_CURRENT_SOURCE_DIR}/_deps)
        execute_process(COMMAND rm -rf ${CMAKE_CURRENT_SOURCE_DIR}/script/_deps)
    endif()
    enable_testing()
    include(cmake/gtest.cmake)
    add_subdirectory(test)
    
endfunction()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")

read_config()

target_link_libraries(${PROJECT_NAME}  ${CMAKE_THREAD_LIBS_INIT})
