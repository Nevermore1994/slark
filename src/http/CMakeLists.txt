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

macro(fetch_openssl)
    set(OPENSSL_URL "https://github.com/Nevermore1994/openssl/archive/refs/tags/3.0.13.zip")
    set(OPENSSL_DOWNLOAD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/openssl)
    set(OPENSSL_UNPACK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/openssl)

    message("download openssl lib ${OPENSSL_DOWNLOAD_DIR}")
    file(DOWNLOAD
            ${OPENSSL_URL}
            ${OPENSSL_DOWNLOAD_DIR}/openssl.zip
            SHOW_PROGRESS
    )

    message("download openssl lib ${OPENSSL_UNPACK_DIR}")
    file(ARCHIVE_EXTRACT
            INPUT ${OPENSSL_DOWNLOAD_DIR}/openssl.zip
            DESTINATION ${OPENSSL_UNPACK_DIR}
    )
    file(REMOVE ${OPENSSL_DOWNLOAD_DIR}/openssl.zip)
endmacro()

if (NOT DISABLE_HTTPS)
    message(STATUS "enable https")
    fetch_openssl()
    if (${BUILD_PLATFORM} STREQUAL "iOS")
        set(OPENSSL_LIBRARY_ROOT ../third_party/openssl/ios)
        set(SSL_LIBRARY ${OPENSSL_LIBRARY_ROOT}/libssl.a)
        set(CRYPTO_LIBRARY ${OPENSSL_LIBRARY_ROOT}libcrypto.a)
        target_link_libraries(${MODULE_NAME} PUBLIC ${SSL_LIBRARY} ${CRYPTO_LIBRARY})
        target_include_directories(${MODULE_NAME} PUBLIC ${OPENSSL_LIBRARY_ROOT}/include)
        target_compile_definitions(${MODULE_NAME} PUBLIC SLARK_IOS)
        target_include_directories(${MODULE_NAME} PRIVATE ../platform/iOS/)
    elseif(${BUILD_PLATFORM} STREQUAL "Android")
        set(OPENSSL_LIBRARY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/openssl/android)
        set(SSL_LIBRARY ${OPENSSL_LIBRARY_ROOT}/libssl.a)
        set(CRYPTO_LIBRARY ${OPENSSL_LIBRARY_ROOT}/libcrypto.a)
        target_link_libraries(${MODULE_NAME} PUBLIC ${SSL_LIBRARY} ${CRYPTO_LIBRARY})
        target_include_directories(${MODULE_NAME} PUBLIC ${OPENSSL_LIBRARY_ROOT}/include)
        target_compile_definitions(${MODULE_NAME} PUBLIC SLARK_ANDROID)
        target_include_directories(${MODULE_NAME} PRIVATE ../platform/Android/sdk/src/main/cpp)
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