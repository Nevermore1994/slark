cmake_minimum_required(VERSION 3.20)

include(CMakeForceCompiler)
include(CMakePrintHelpers)
include(FetchContent)

#C++ standard
set(CMAKE_CXX_FLAGS "-std=c++17 -Wall -Wextra -pedantic -Wcast-align -Wcast-qual -Wconversion -Wdisabled-optimization -Wendif-labels -Wfloat-equal -Winit-self -Winline -Wmissing-include-dirs -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wpacked -Wpointer-arith -Wredundant-decls -Wshadow -Wsign-promo -Wvariadic-macros -Wwrite-strings ")
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -pg")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -s -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-g3 -Og -pg")
set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -s -DNDEBUG -march=native")

message(STATUS "current path: " ${CMAKE_CURRENT_SOURCE_DIR})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")

project(slark)

#set thread library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


#source file path
set(BASE_DIR slark/base/)
set(THIRD_PARTY_DIR slark/third_party/)
set(CORE_DIR slark/core/)


set(BASE_LIBRARY "base")
set(CORE_LIBRARY "core")

function(add_base_library)
    message(STATUS "add base library...")
    FILE(GLOB BASE_FILES ${BASE_DIR}/*.cpp ${BASE_DIR}/*.hpp)
    target_sources(${PROJECT_NAME} PUBLIC ${BASE_FILES})
    target_include_directories(${PROJECT_NAME} PUBLIC ${BASE_DIR})
    target_include_directories(${PROJECT_NAME} PUBLIC ${THIRD_PARTY_DIR})
    source_group("base" FILES ${BASE_FILES})
    message(STATUS ${BASE_FILES})
endfunction()

function(add_core_library)
    message(STATUS "add core library...")
    FILE(GLOB CORE_FILES ${CORE_DIR}/*.cpp ${CORE_DIR}/*.hpp)
    target_sources(${PROJECT_NAME} PUBLIC ${CORE_FILES})
    target_include_directories(${PROJECT_NAME} PUBLIC ${CORE_DIR})
    source_group("core" FILES ${CORE_FILES})
endfunction()

function(add_component name)
    message(STATUS "add ${name} library...")
    FILE(GLOB FILES ${CORE_DIR}/${name}/*.cpp ${CORE_DIR}/${name}/*.hpp)
    target_sources(${PROJECT_NAME} PUBLIC ${FILES})
    target_include_directories(${PROJECT_NAME} PUBLIC ${CORE_DIR}/${name})
    source_group("core/${name}" FILES ${FILES})
endfunction()

function(add_ios_library)
    message(STATUS "add ios library...")
endfunction()

function(add_android_library)
    message(STATUS "add android library...")
endfunction()

#set_build_type
function(set_build_type build_library build_type is_build_executable)
    if (${build_type} STREQUAL "Debug")
        set(CMAKE_BUILD_TYPE DEBUG PARENT_SCOPE)
        add_definitions(-DDEBUG) #set DEBUG = 1
        add_executable(${PROJECT_NAME} "")
    else ()
        set(CMAKE_BUILD_TYPE RELEASE PARENT_SCOPE)
        add_library(${PROJECT_NAME} "")
    endif ()

    #must add base library
    add_base_library()

    #add core library
    if (${build_library} STREQUAL ${CORE_LIBRARY})
        add_core_library()
        add_component("codec")
        add_component("muxer")
        add_component("audio")
    elseif (NOT (${build_library} STREQUAL ${BASE_LIBRARY}))
        message(STATUS "build library parameter error...")
    endif ()

    if (${is_build_executable} EQUAL 1)
        if (${build_library} STREQUAL ${BASE_LIBRARY})
            #base test
            target_sources(${PROJECT_NAME} PUBLIC slark/baseMain.cpp)
            source_group("Source Files" FILES slark/baseMain.cpp)
        elseif (${build_library} STREQUAL ${CORE_LIBRARY})
            target_sources(${PROJECT_NAME} PUBLIC slark/main.cpp)
            source_group("Source Files" FILES slark/main.cpp)
        endif ()
        message(STATUS "will build testing...")
    elseif (${is_build_executable} EQUAL 0})
        message(STATUS "will build library...")
    else ()
        message(STATUS "build executable parameters error...")
    endif ()
endfunction(set_build_type)

#set build type
set_build_type(${BASE_LIBRARY} "Debug" 1)

if (CMAKE_BUILD_TYPE STREQUAL DEBUG)
    message("build testing....")
    enable_testing()
    include(cmake/gtest.cmake)
    add_subdirectory(test)
else ()
    message("build library....")
endif ()

target_link_libraries(${PROJECT_NAME}  ${CMAKE_THREAD_LIBS_INIT})
