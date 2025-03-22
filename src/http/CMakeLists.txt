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

if (NOT DISABLE_HTTPS)
    message(STATUS "enable https")
    if (${BUILD_PLATFORM} STREQUAL "iOS")
        set(OPENSSL_LIBRARY_ROOT ../libs/openssl/ios)
        set(SSL_LIBRARY ${OPENSSL_LIBRARY_ROOT}/libssl.a)
        set(CRYPTO_LIBRARY ${OPENSSL_LIBRARY_ROOT}libcrypto.a)
        target_link_libraries(${MODULE_NAME} PUBLIC ${SSL_LIBRARY} ${CRYPTO_LIBRARY})
        target_include_directories(${MODULE_NAME} PUBLIC ${OPENSSL_LIBRARY_ROOT}/include)
    elseif(${BUILD_PLATFORM} STREQUAL "Android")
        set(OPENSSL_LIBRARY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/openssl/android)
        set(SSL_LIBRARY ${OPENSSL_LIBRARY_ROOT}/libssl.so)
        set(CRYPTO_LIBRARY ${OPENSSL_LIBRARY_ROOT}/libcrypto.so)
        target_link_libraries(${MODULE_NAME} PUBLIC ${SSL_LIBRARY} ${CRYPTO_LIBRARY})
        target_include_directories(${MODULE_NAME} PUBLIC ${OPENSSL_LIBRARY_ROOT}/include)
    else()
        find_package(OpenSSL REQUIRED)
        message(STATUS "OPENSSL_FOUND: ${OPENSSL_FOUND}")
        message(STATUS "OPENSSL_INCLUDE_DIR: ${OPENSSL_INCLUDE_DIR}")
        message(STATUS "OPENSSL_LIBRARIES: ${OPENSSL_LIBRARIES}")
        message(STATUS "OPENSSL_CRYPTO_LIBRARY: ${OPENSSL_CRYPTO_LIBRARY}")
        target_link_libraries(${MODULE_NAME} PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    endif()
    target_compile_definitions(${MODULE_NAME} PUBLIC ENABLE_HTTPS)
else()
    message(STATUS "disable https")
endif ()

target_include_directories(${MODULE_NAME} PUBLIC ../base)
target_link_libraries(${MODULE_NAME} PUBLIC ${CMAKE_THREAD_LIBS_INIT})