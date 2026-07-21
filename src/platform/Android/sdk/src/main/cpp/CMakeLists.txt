cmake_minimum_required(VERSION 3.22.1)

project("slark_jni")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


message(STATUS "slark_jni build path: " ${SLARK_PROJECT_PATH})

file(GLOB BUSINESS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

# 编译 slark_jni 静态库
add_library(slark_jni STATIC ${BUSINESS_SOURCES})

target_include_directories(slark_jni PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/jni
    ${SLARK_PROJECT_PATH}/src/base
    ${SLARK_PROJECT_PATH}/src/core
    ${SLARK_PROJECT_PATH}/src/core/demuxer
    ${SLARK_PROJECT_PATH}/src/core/audio
    ${SLARK_PROJECT_PATH}/src/core/codec
    ${SLARK_PROJECT_PATH}/src/core/pipeline
    ${SLARK_PROJECT_PATH}/src/core/perf
    ${SLARK_PROJECT_PATH}/src/http
    ${SLARK_PROJECT_PATH}/include
    ${SLARK_PROJECT_PATH}/internal
)

target_compile_definitions(slark_jni PUBLIC
    SLARK_ANDROID
    ANDROID_PLATFORM
    __ANDROID__
)

find_library(ANDROID_LOG_LIB log)
find_library(ANDROID_LIB android)
find_library(ANDROID_GLESv3_LIB GLESv3)
find_library(ANDROID_EGL_LIB EGL)

target_link_libraries(slark_jni
    PUBLIC
    slark_jni_common
    ${ANDROID_LOG_LIB}
    ${ANDROID_LIB}
    ${ANDROID_GLESv3_LIB}
    ${ANDROID_EGL_LIB}
    aaudio
    vulkan
)

# OpenSSL include 路径：AndroidPlatformInit.cpp 需要 openssl/ssl.h
# slark_jni 是静态库，只需要头文件，链接由顶层 slark 共享库负责
if(NOT DISABLE_HTTP)
    include(${SLARK_PROJECT_PATH}/cmake/OpenSSL.cmake)
    # create_openssl_target() 内部定义 OPENSSL_INSTALL_DIR 和 slark::openssl
    # 这里只借 include 路径，不 link
    if(NOT TARGET slark::openssl)
        create_openssl_target()
    endif()
    get_target_property(_ssl_inc_dir slark::openssl INTERFACE_INCLUDE_DIRECTORIES)
    if(_ssl_inc_dir)
        target_include_directories(slark_jni PUBLIC ${_ssl_inc_dir})
    endif()
endif()