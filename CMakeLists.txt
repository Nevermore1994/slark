cmake_minimum_required(VERSION 3.20)

include(CMakeForceCompiler)
include(CMakePrintHelpers)
include(FetchContent)

#C++ standard
set(CMAKE_CXX_FLAGS "-std=c++17 -Wall -Wextra -pedantic -Wcast-align -Wcast-qual -Wconversion -Wdisabled-optimization -Wendif-labels -Wfloat-equal -Winit-self -Winline -Wmissing-include-dirs -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wpacked -Wpointer-arith -Wredundant-decls -Wshadow -Wsign-promo -Wswitch-default -Wvariadic-macros -Wwrite-strings ")
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -pg")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -s -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-g3 -Og -pg")
set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -s -DNDEBUG -march=native")

message(STATUS "current path: " ${CMAKE_CURRENT_SOURCE_DIR})

project(slark)

#set thread library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)


#source file path
set(BASE_INCLUDE_DIR slark/base/headers)
set(THIRD_PARTY_INCLUDE_DIR slark/third_party)
set(CORE_INCLUDE_DIR slark/core/headers)
set(AUDIO_INCLUDE_DIR slark/core/audio/headers)

#base src files
aux_source_directory(slark/base/src BASE_DIR_SRCS)
#core src files
aux_source_directory(slark/core/src CORE_DIR_SRCS)
#core base testing files
aux_source_directory(test/ BASE_TEST_DIR_SRCS)
#core core testing files
aux_source_directory(slark/core/main.cpp CORE_TEST_DIR_SRCS)
#audio src files
aux_source_directory(slark/core/audio AUDIO_DIR_SRCS)

#release
set(RELEASE_BASE_LIBRARY 0)
set(RELEASE_CORE_LIBRARY 1)
set(RELEASE_ALL_LIBRARY 100)
#test type
set(TESTING_BASE_TYPE 101)
set(TESTING_CORE_TYPE 102)
set(TESTING_ALL_TYPE 103)

#set_build_type
function(set_build_type build_type)
    if (${build_type} GREATER ${RELEASE_ALL_LIBRARY})
        set(CMAKE_BUILD_TYPE DEBUG PARENT_SCOPE)
        add_executable(${PROJECT_NAME} "")
    else()
        set(CMAKE_BUILD_TYPE RELEASE PARENT_SCOPE)
        add_library(${PROJECT_NAME} "")
    endif ()

    target_sources(${PROJECT_NAME} PUBLIC ${BASE_DIR_SRCS})
    target_include_directories(${PROJECT_NAME} PUBLIC ${BASE_INCLUDE_DIR})
    target_include_directories(${PROJECT_NAME} PUBLIC ${THIRD_PARTY_INCLUDE_DIR})
    if (${build_type} EQUAL ${TESTING_BASE_TYPE})
        #base test
        target_sources(${PROJECT_NAME} PRIVATE test/main.cpp)
    elseif (${build_type} EQUAL ${TESTING_CORE_TYPE})
        #core test
        target_sources(${PROJECT_NAME} PRIVATE slark/core/main.cpp)
        target_sources(${PROJECT_NAME} PUBLIC ${CORE_DIR_SRCS})
        target_include_directories(${PROJECT_NAME} PRIVATE ${CORE_INCLUDE_DIR})
    elseif (${build_type} EQUAL ${TESTING_ALL_TYPE})
        #all test
        target_sources(${PROJECT_NAME} PRIVATE slark/core/main.cpp)
        target_sources(${PROJECT_NAME} PUBLIC ${CORE_DIR_SRCS} PUBLIC ${CORE_INCLUDE_DIR})
        target_sources(${PROJECT_NAME} PUBLIC ${AUDIO_DIR_SRCS} PUBLIC ${AUDIO_INCLUDE_DIR})
        target_include_directories(${PROJECT_NAME} PUBLIC ${CORE_INCLUDE_DIR})
        target_include_directories(${PROJECT_NAME} PUBLIC ${AUDIO_INCLUDE_DIR})
    endif ()
endfunction(set_build_type)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
#set build type
set_build_type(${TESTING_CORE_TYPE})

if (CMAKE_BUILD_TYPE STREQUAL DEBUG)
    message("build testing....")
else ()
    message("build library....")
endif ()

target_link_libraries(${PROJECT_NAME}  ${CMAKE_THREAD_LIBS_INIT})

# tests
if(NOT without-test)
    enable_testing()
    include(cmake/gtest.cmake)
    add_subdirectory(test)
endif()