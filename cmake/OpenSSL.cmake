# OpenSSL Source Build Module for Slark
# Compiles OpenSSL 3.0.13 from source with cross-platform support
#
# Usage:
# include(cmake/OpenSSL.cmake)
# target_link_libraries(your_target PRIVATE slark::openssl)
#
# This module provides:
# - Automatic source download and caching
# - Cross-compilation support for iOS and Android only
# - Out-of-source build: iOS and Android each use an isolated build directory,
# the source tree is never modified, preventing cross-platform artifact pollution.

include_guard()
include(FetchContent)

# =============================================================================
# Configuration
# =============================================================================
set(OPENSSL_VERSION "3.0.13")
set(OPENSSL_SOURCE_URL "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz")
set(OPENSSL_MIRROR_URL "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz")

# 源码缓存目录（只解压一份，所有平台共享，不在此目录产生编译产物）
set(OPENSSL_SOURCE_CACHE_DIR "${CMAKE_SOURCE_DIR}/.fetchcontent_cache/openssl")

# 安装目录按平台隔离
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(OPENSSL_INSTALL_DIR "${CMAKE_BINARY_DIR}/deps/openssl/ios")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(OPENSSL_INSTALL_DIR "${CMAKE_BINARY_DIR}/deps/openssl/android")
else()
    set(OPENSSL_INSTALL_DIR "${CMAKE_BINARY_DIR}/deps/openssl")
endif()

# =============================================================================
# Platform Detection
# =============================================================================
function(_openssl_detect_platform)
    if(DEFINED CMAKE_SYSTEM_NAME)
        if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
            set(OPENSSL_TARGET_PLATFORM "ios" PARENT_SCOPE)
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
            set(OPENSSL_TARGET_PLATFORM "android" PARENT_SCOPE)
        else()
            message(FATAL_ERROR "OpenSSL build only supports iOS and Android platforms. Current: ${CMAKE_SYSTEM_NAME}")
        endif()
    else()
        message(FATAL_ERROR "CMAKE_SYSTEM_NAME not defined. This module only supports cross-compilation for iOS/Android.")
    endif()
endfunction()

# =============================================================================
# Source Download with Cache
# =============================================================================
function(_openssl_download_source)
    set(OPENSSL_SOURCE_DIR "${OPENSSL_SOURCE_CACHE_DIR}/openssl-${OPENSSL_VERSION}")

    if(EXISTS "${OPENSSL_SOURCE_DIR}/Configure")
        message(STATUS "OpenSSL ${OPENSSL_VERSION} source found in cache: ${OPENSSL_SOURCE_DIR}")
        set(OPENSSL_SOURCE_DIR "${OPENSSL_SOURCE_DIR}" PARENT_SCOPE)
        return()
    endif()

    message(STATUS "Downloading OpenSSL ${OPENSSL_VERSION} source...")
    file(MAKE_DIRECTORY "${OPENSSL_SOURCE_CACHE_DIR}")

    set(DOWNLOAD_URLS "${OPENSSL_SOURCE_URL}" "${OPENSSL_MIRROR_URL}")

    foreach(URL IN LISTS DOWNLOAD_URLS)
        message(STATUS "Trying to download from: ${URL}")
        file(DOWNLOAD
            "${URL}"
            "${OPENSSL_SOURCE_CACHE_DIR}/openssl-${OPENSSL_VERSION}.tar.gz"
            SHOW_PROGRESS
            STATUS DOWNLOAD_STATUS
        )
        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)

        if(STATUS_CODE EQUAL 0)
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "openssl-${OPENSSL_VERSION}.tar.gz"
                WORKING_DIRECTORY "${OPENSSL_SOURCE_CACHE_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
            )
            file(REMOVE "${OPENSSL_SOURCE_CACHE_DIR}/openssl-${OPENSSL_VERSION}.tar.gz")

            if(EXTRACT_RESULT EQUAL 0 AND EXISTS "${OPENSSL_SOURCE_DIR}/Configure")
                message(STATUS "OpenSSL ${OPENSSL_VERSION} source downloaded and extracted successfully")
                set(OPENSSL_SOURCE_DIR "${OPENSSL_SOURCE_DIR}" PARENT_SCOPE)
                return()
            endif()
        endif()

        message(WARNING "Failed to download from ${URL}, trying next mirror...")
    endforeach()

    message(FATAL_ERROR "Failed to download OpenSSL ${OPENSSL_VERSION} from all sources")
endfunction()

# =============================================================================
# iOS Cross-Compilation（out-of-source build）
#
# 源码目录只读，所有编译产物写入独立的 build 目录，
# 与 Android 构建完全隔离，不会互相污染。
# =============================================================================
function(_openssl_build_ios OPENSSL_SOURCE_DIR)
    message(STATUS "Building OpenSSL for iOS (out-of-source)...")

    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(IOS_ARCH "arm64")
    else()
        list(GET CMAKE_OSX_ARCHITECTURES 0 IOS_ARCH)
    endif()

    if(IOS_ARCH STREQUAL "arm64")
        set(OPENSSL_TARGET "ios64-xcrun")
    elseif(IOS_ARCH STREQUAL "x86_64")
        set(OPENSSL_TARGET "iossimulator-xcrun")
    elseif(IOS_ARCH STREQUAL "arm64-sim")
        set(OPENSSL_TARGET "iossimulator-xcrun")
    else()
        message(FATAL_ERROR "Unsupported iOS architecture: ${IOS_ARCH}")
    endif()

    if(NOT DEFINED IOS_DEPLOYMENT_TARGET)
        set(IOS_DEPLOYMENT_TARGET "16.5")
    endif()

    # 独立 build 目录，与源码树和 Android build 目录完全隔离
    set(OPENSSL_BUILD_DIR "${CMAKE_BINARY_DIR}/deps/openssl/build-ios-${IOS_ARCH}")
    file(MAKE_DIRECTORY "${OPENSSL_BUILD_DIR}")

    # OpenSSL Configure 不支持真正的 out-of-source，需要先把源码复制到 build 目录
    # 用 rsync 只同步源文件（排除 .o/.a/.so 等产物），保持源码缓存干净
    execute_process(
        COMMAND sh -c "rsync -a --exclude='*.o' --exclude='*.a' --exclude='*.so' \
            --exclude='*.dylib' --exclude='Makefile' --exclude='configdata.pm' \
            '${OPENSSL_SOURCE_DIR}/' '${OPENSSL_BUILD_DIR}/'"
        RESULT_VARIABLE RSYNC_RESULT
    )

    if(NOT RSYNC_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to copy OpenSSL source to build directory")
    endif()

    # no-shared: 静态库  no-tests: 跳过测试  no-legacy/no-module: iOS 不允许动态 provider
    execute_process(
        COMMAND sh -c
        "CC=clang CFLAGS='-target ${IOS_ARCH}-apple-ios${IOS_DEPLOYMENT_TARGET}' \
             ./Configure ${OPENSSL_TARGET} \
             --prefix=${OPENSSL_INSTALL_DIR} \
             --openssldir=${OPENSSL_INSTALL_DIR}/ssl \
             no-shared no-tests no-legacy no-module"
        WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
        RESULT_VARIABLE CONFIGURE_RESULT
    )

    if(NOT CONFIGURE_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenSSL configure failed for iOS")
    endif()

    include(ProcessorCount)
    ProcessorCount(NPROC)

    # 只构建库，跳过 apps/openssl 可执行文件（iOS 上无法链接）
    execute_process(
        COMMAND make -j${NPROC} build_libs
        WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
        RESULT_VARIABLE BUILD_RESULT
    )

    if(NOT BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenSSL build failed for iOS")
    endif()

    # install_dev 只安装头文件和静态库
    execute_process(
        COMMAND make install_dev
        WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
        RESULT_VARIABLE INSTALL_RESULT
    )

    if(NOT INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenSSL install failed for iOS")
    endif()

    # Apple 链接器要求 libtool 格式（ar archive random library）
    # OpenSSL 的 install_dev 使用 GNU ranlib，产出 GNU ar 格式，需要重新打包：
    # 先用 ar -x 提取所有 .o，再用 Apple libtool 重新打包（libtool 不能直接处理 GNU ar 格式）
    foreach(_lib ssl crypto)
        set(_lib_path "${OPENSSL_INSTALL_DIR}/lib/lib${_lib}.a")

        if(EXISTS "${_lib_path}")
            set(_repack_dir "${OPENSSL_BUILD_DIR}/repack_${_lib}")
            execute_process(
                COMMAND sh -c "rm -rf '${_repack_dir}' && mkdir -p '${_repack_dir}' \
                    && cd '${_repack_dir}' && ar -x '${_lib_path}' \
                    && libtool -static -o '${_lib_path}' *.o"
                RESULT_VARIABLE LIBTOOL_RESULT
            )

            if(NOT LIBTOOL_RESULT EQUAL 0)
                message(WARNING "libtool repack failed for lib${_lib}.a")
            else()
                message(STATUS "Repacked lib${_lib}.a with Apple libtool")
            endif()
        endif()
    endforeach()

    message(STATUS "OpenSSL for iOS built and installed to: ${OPENSSL_INSTALL_DIR}")
endfunction()

# =============================================================================
# Android Cross-Compilation（out-of-source build）
#
# 源码目录只读，所有编译产物写入独立的 build 目录，
# 与 iOS 构建完全隔离，不会互相污染。
# =============================================================================
function(_openssl_build_android OPENSSL_SOURCE_DIR)
    message(STATUS "Building OpenSSL for Android (out-of-source)...")

    if(NOT DEFINED ANDROID_ABI)
        set(ANDROID_ABI "arm64-v8a")
    endif()

    if(ANDROID_ABI STREQUAL "arm64-v8a")
        set(OPENSSL_TARGET "android-arm64")
    elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
        set(OPENSSL_TARGET "android-arm")
    elseif(ANDROID_ABI STREQUAL "x86_64")
        set(OPENSSL_TARGET "android-x86_64")
    elseif(ANDROID_ABI STREQUAL "x86")
        set(OPENSSL_TARGET "android-x86")
    else()
        message(FATAL_ERROR "Unsupported Android ABI: ${ANDROID_ABI}")
    endif()

    if(NOT DEFINED ANDROID_NDK)
        if(DEFINED CMAKE_ANDROID_NDK)
            set(ANDROID_NDK "${CMAKE_ANDROID_NDK}")
        elseif(DEFINED ENV{ANDROID_NDK_ROOT})
            set(ANDROID_NDK "$ENV{ANDROID_NDK_ROOT}")
        elseif(DEFINED ENV{ANDROID_NDK_HOME})
            set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
        else()
            message(FATAL_ERROR "ANDROID_NDK not set. Please set ANDROID_NDK or ANDROID_NDK_ROOT environment variable")
        endif()
    endif()

    if(NOT DEFINED ANDROID_PLATFORM)
        set(ANDROID_PLATFORM "android-29")
    endif()

    string(REGEX REPLACE "android-([0-9]+)" "\\1" ANDROID_API_LEVEL "${ANDROID_PLATFORM}")

    set(NDK_TOOLCHAIN_BIN "${ANDROID_NDK}/toolchains/llvm/prebuilt/darwin-x86_64/bin")

    if(NOT EXISTS "${NDK_TOOLCHAIN_BIN}")
        set(NDK_TOOLCHAIN_BIN "${ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/bin")
    endif()

    # 独立 build 目录，与源码树和 iOS build 目录完全隔离
    set(OPENSSL_BUILD_DIR "${CMAKE_BINARY_DIR}/deps/openssl/build-android-${ANDROID_ABI}")
    file(MAKE_DIRECTORY "${OPENSSL_BUILD_DIR}")

    # 同步源码到独立 build 目录（排除产物文件）
    execute_process(
        COMMAND sh -c "rsync -a --exclude='*.o' --exclude='*.a' --exclude='*.so' \
            --exclude='*.dylib' --exclude='Makefile' --exclude='configdata.pm' \
            '${OPENSSL_SOURCE_DIR}/' '${OPENSSL_BUILD_DIR}/'"
        RESULT_VARIABLE RSYNC_RESULT
    )

    if(NOT RSYNC_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to copy OpenSSL source to build directory")
    endif()

    execute_process(
        COMMAND sh -c
        "ANDROID_NDK_ROOT='${ANDROID_NDK}' PATH='${NDK_TOOLCHAIN_BIN}:/usr/bin:/bin:$PATH' \
             /usr/bin/perl ./Configure ${OPENSSL_TARGET} \
             --prefix=${OPENSSL_INSTALL_DIR} \
             --openssldir=${OPENSSL_INSTALL_DIR}/ssl \
             -D__ANDROID_API__=${ANDROID_API_LEVEL} \
             shared no-tests"
        WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
        RESULT_VARIABLE CONFIGURE_RESULT
    )

    if(NOT CONFIGURE_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenSSL configure failed for Android")
    endif()

    include(ProcessorCount)
    ProcessorCount(NPROC)

    execute_process(
        COMMAND sh -c "PATH='${NDK_TOOLCHAIN_BIN}:/usr/bin:/bin:$PATH' make -j${NPROC}"
        WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
        RESULT_VARIABLE BUILD_RESULT
    )

    if(NOT BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenSSL build failed for Android")
    endif()

    execute_process(
        COMMAND sh -c "PATH='${NDK_TOOLCHAIN_BIN}:/usr/bin:/bin:$PATH' make install_sw"
        WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
        RESULT_VARIABLE INSTALL_RESULT
    )

    if(NOT INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenSSL install failed for Android")
    endif()

    message(STATUS "OpenSSL for Android built and installed to: ${OPENSSL_INSTALL_DIR}")
endfunction()

# =============================================================================
# Main Function: Build OpenSSL
# =============================================================================
function(build_openssl)
    _openssl_detect_platform()

    _openssl_download_source()

    # Android 用动态库，iOS 用静态库
    if(OPENSSL_TARGET_PLATFORM STREQUAL "android")
        if(EXISTS "${OPENSSL_INSTALL_DIR}/lib/libssl.so" OR EXISTS "${OPENSSL_INSTALL_DIR}/lib64/libssl.so")
            message(STATUS "OpenSSL already built at: ${OPENSSL_INSTALL_DIR}")
            return()
        endif()
    else()
        # iOS 静态库：必须是 Apple libtool 格式（ar archive random library）
        # GNU ar 格式（current ar archive）会导致链接器报 "archive member '/' not a mach-o file"
        set(_ssl_lib "")

        if(EXISTS "${OPENSSL_INSTALL_DIR}/lib/libssl.a")
            set(_ssl_lib "${OPENSSL_INSTALL_DIR}/lib/libssl.a")
        elseif(EXISTS "${OPENSSL_INSTALL_DIR}/lib64/libssl.a")
            set(_ssl_lib "${OPENSSL_INSTALL_DIR}/lib64/libssl.a")
        endif()

        if(_ssl_lib)
            execute_process(
                COMMAND file "${_ssl_lib}"
                OUTPUT_VARIABLE _file_output
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )

            if(_file_output MATCHES "ar archive random library")
                message(STATUS "OpenSSL already built at: ${OPENSSL_INSTALL_DIR}")
                return()
            else()
                message(STATUS "OpenSSL cache is GNU ar format, rebuilding with Apple libtool...")
                file(REMOVE_RECURSE "${OPENSSL_INSTALL_DIR}")
            endif()
        endif()
    endif()

    if(OPENSSL_TARGET_PLATFORM STREQUAL "ios")
        _openssl_build_ios("${OPENSSL_SOURCE_DIR}")
    elseif(OPENSSL_TARGET_PLATFORM STREQUAL "android")
        _openssl_build_android("${OPENSSL_SOURCE_DIR}")
    endif()
endfunction()

# =============================================================================
# Create CMake Target
# =============================================================================
function(create_openssl_target)
    build_openssl()

    if(CMAKE_SYSTEM_NAME STREQUAL "Android")
        if(EXISTS "${OPENSSL_INSTALL_DIR}/lib/libssl.so")
            set(SSL_LIBRARY "${OPENSSL_INSTALL_DIR}/lib/libssl.so")
            set(CRYPTO_LIBRARY "${OPENSSL_INSTALL_DIR}/lib/libcrypto.so")
        elseif(EXISTS "${OPENSSL_INSTALL_DIR}/lib64/libssl.so")
            set(SSL_LIBRARY "${OPENSSL_INSTALL_DIR}/lib64/libssl.so")
            set(CRYPTO_LIBRARY "${OPENSSL_INSTALL_DIR}/lib64/libcrypto.so")
        else()
            set(SSL_LIBRARY "SSL_LIBRARY-NOTFOUND")
            set(CRYPTO_LIBRARY "CRYPTO_LIBRARY-NOTFOUND")
        endif()
    else()
        unset(SSL_LIBRARY CACHE)
        unset(CRYPTO_LIBRARY CACHE)
        find_library(SSL_LIBRARY
            NAMES ssl libssl
            PATHS "${OPENSSL_INSTALL_DIR}/lib" "${OPENSSL_INSTALL_DIR}/lib64"
            NO_DEFAULT_PATH
        )
        find_library(CRYPTO_LIBRARY
            NAMES crypto libcrypto
            PATHS "${OPENSSL_INSTALL_DIR}/lib" "${OPENSSL_INSTALL_DIR}/lib64"
            NO_DEFAULT_PATH
        )
    endif()

    if(NOT SSL_LIBRARY OR SSL_LIBRARY STREQUAL "SSL_LIBRARY-NOTFOUND" OR
        NOT CRYPTO_LIBRARY OR CRYPTO_LIBRARY STREQUAL "CRYPTO_LIBRARY-NOTFOUND")
        message(FATAL_ERROR "OpenSSL libraries not found after build. Install dir: ${OPENSSL_INSTALL_DIR}")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Android")
        # Android 动态库：两个 target 分开，Gradle 才能把两个 .so 都打入 AAR
        add_library(slark::openssl_crypto SHARED IMPORTED GLOBAL)
        set_target_properties(slark::openssl_crypto PROPERTIES
            IMPORTED_LOCATION "${CRYPTO_LIBRARY}"
        )
        add_library(slark::openssl SHARED IMPORTED GLOBAL)
        set_target_properties(slark::openssl PROPERTIES
            IMPORTED_LOCATION "${SSL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL_DIR}/include"
            INTERFACE_LINK_LIBRARIES "slark::openssl_crypto"
        )
    else()
        # iOS 静态库
        add_library(slark::openssl STATIC IMPORTED GLOBAL)
        set_target_properties(slark::openssl PROPERTIES
            IMPORTED_LOCATION "${SSL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL_DIR}/include"
            INTERFACE_LINK_LIBRARIES "${CRYPTO_LIBRARY}"
        )
    endif()

    message(STATUS "OpenSSL target created: slark::openssl")
    message(STATUS "  SSL Library: ${SSL_LIBRARY}")
    message(STATUS "  Crypto Library: ${CRYPTO_LIBRARY}")
    message(STATUS "  Include Dir: ${OPENSSL_INSTALL_DIR}/include")
endfunction()

# =============================================================================
# Convenience function for targets
# =============================================================================
function(target_link_openssl TARGET)
    if(NOT TARGET slark::openssl)
        create_openssl_target()
    endif()

    target_link_libraries(${TARGET} ${ARGN} slark::openssl)
endfunction()
