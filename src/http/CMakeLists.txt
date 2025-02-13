cmake_minimum_required(VERSION 3.20)

#set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@1.1")
#set(OPENSSL_INCLUDE_DIR "/opt/homebrew/opt/openssl@1.1/include")

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

if (NOT DISABLE_HTTPS)
    message(STATUS "enable https")
    target_compile_definitions(${MODULE_NAME} PUBLIC ENABLE_HTTPS)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${MODULE_NAME} OpenSSL::SSL OpenSSL::Crypto)
else()
    message(STATUS "disable https")
endif ()

target_include_directories(${MODULE_NAME} PUBLIC ../base)
target_link_libraries(${MODULE_NAME} ${CMAKE_THREAD_LIBS_INIT})