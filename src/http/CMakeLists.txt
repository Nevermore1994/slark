cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MODULE_NAME http_request)

set(SRC_FILE_LISTS *.cpp)
set(PRIVATE_HEADER_FILE_LIST include/*.hpp include/*.h)
set(PUBLIC_HEADER_FILE_LIST include/public/*.hpp include/public/*.h)

find_package(Threads REQUIRED)

file(GLOB FILES ${SRC_FILE_LISTS} ${PRIVATE_HEADER_FILE_LIST} ${PUBLIC_HEADER_FILE_LIST})

add_library(${MODULE_NAME} ${FILES})

target_include_directories(${MODULE_NAME} PRIVATE include)
target_include_directories(${MODULE_NAME} PUBLIC include/public)

option(DISABLE_HTTPS "disable https" OFF)

if(NOT DISABLE_HTTPS)
    message(STATUS "enable https")

    # Use OpenSSL source build module
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/OpenSSL.cmake)
    target_link_openssl(${MODULE_NAME} PUBLIC)

    # Platform-specific settings
    if(${BUILD_PLATFORM} STREQUAL "iOS")
        target_compile_definitions(${MODULE_NAME} PUBLIC SLARK_IOS)
        target_include_directories(${MODULE_NAME} PRIVATE ../platform/iOS/)
    elseif(${BUILD_PLATFORM} STREQUAL "Android")
        target_compile_definitions(${MODULE_NAME} PUBLIC SLARK_ANDROID)
        target_include_directories(${MODULE_NAME} PRIVATE ../platform/Android/sdk/src/main/cpp)
    endif()
else()
    message(STATUS "disable https")
endif()

target_link_libraries(${MODULE_NAME} PUBLIC slark_base ${CMAKE_THREAD_LIBS_INIT})