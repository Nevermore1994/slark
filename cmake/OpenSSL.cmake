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

set(OPENSSL_SOURCE_CACHE_DIR "${CMAKE_SOURCE_DIR}/.fetchcontent_cache/openssl")

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # iOS 不支持 fat library（真机 arm64 和模拟器 arm64 架构名相同但二进制不兼容）
    # 根据当前 SDK 选择对应的 OpenSSL 安装目录
    if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
        set(OPENSSL_INSTALL_DIR "${CMAKE_BINARY_DIR}/deps/openssl/iphonesimulator")
    else()
        set(OPENSSL_INSTALL_DIR "${CMAKE_BINARY_DIR}/deps/openssl/iphoneos")
    endif()
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
# Note: Cannot create fat library because device arm64 and simulator arm64
# have the same arch name but incompatible binaries.
# We build separately for iphoneos and iphonesimulator.
# =============================================================================
function(_openssl_build_ios OPENSSL_SOURCE_DIR)
    message(STATUS "Building OpenSSL for iOS (out-of-source)...")

    if(NOT DEFINED IOS_DEPLOYMENT_TARGET)
        set(IOS_DEPLOYMENT_TARGET "16.5")
    endif()

    # Determine which SDK we're building for
    if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
        set(IOS_SDK_NAME "iphonesimulator")

        # 模拟器始终构建 x86_64（Intel Mac / Rosetta）和 arm64（Apple Silicon）两个架构
        set(IOS_ARCHS "x86_64;arm64")
    else()
        set(IOS_SDK_NAME "iphoneos")
        set(IOS_ARCHS "arm64")
    endif()

    message(STATUS "Building OpenSSL for iOS SDK: ${IOS_SDK_NAME}")
    message(STATUS "Architectures: ${IOS_ARCHS}")

    # Build each architecture for this SDK
    set(BUILT_SSL_LIBS "")
    set(BUILT_CRYPTO_LIBS "")

    foreach(IOS_ARCH IN LISTS IOS_ARCHS)
        # Set OpenSSL target and SDK based on architecture and current SDK
        if(IOS_ARCH STREQUAL "arm64" AND IOS_SDK_NAME STREQUAL "iphonesimulator")
            # Simulator arm64 (M1/M2 Mac)
            set(OPENSSL_TARGET "iossimulator-xcrun")
            set(SDK_NAME "iphonesimulator")
        elseif(IOS_ARCH STREQUAL "arm64")
            # Device arm64
            set(OPENSSL_TARGET "ios64-xcrun")
            set(SDK_NAME "iphoneos")
        elseif(IOS_ARCH STREQUAL "x86_64")
            set(OPENSSL_TARGET "iossimulator-xcrun")
            set(SDK_NAME "iphonesimulator")
        else()
            message(WARNING "Unsupported iOS architecture: ${IOS_ARCH}, skipping")
            continue()
        endif()

        message(STATUS "Building OpenSSL for iOS architecture: ${IOS_ARCH} (${SDK_NAME})")

        # Architecture-specific build directory and install directory
        # Each arch installs to its own directory to avoid overwriting each other
        set(OPENSSL_BUILD_DIR "${CMAKE_BINARY_DIR}/deps/openssl/build-${IOS_SDK_NAME}-${IOS_ARCH}")
        set(OPENSSL_ARCH_INSTALL_DIR "${CMAKE_BINARY_DIR}/deps/openssl/${IOS_SDK_NAME}-${IOS_ARCH}")
        file(MAKE_DIRECTORY "${OPENSSL_BUILD_DIR}")
        file(MAKE_DIRECTORY "${OPENSSL_ARCH_INSTALL_DIR}")

        # Copy source to build directory
        execute_process(
            COMMAND sh -c "rsync -a --exclude='*.o' --exclude='*.a' --exclude='*.so' \
                --exclude='*.dylib' --exclude='Makefile' --exclude='configdata.pm' \
                --exclude='*.obj' --exclude='*.lib' \
                '${OPENSSL_SOURCE_DIR}/' '${OPENSSL_BUILD_DIR}/'"
            RESULT_VARIABLE RSYNC_RESULT
        )

        if(NOT RSYNC_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to copy OpenSSL source to build directory for ${IOS_ARCH}")
        endif()

        # 模拟器必须加 -simulator 后缀，否则 clang 会引用 macOS 的 $INODE64 符号
        if(IOS_SDK_NAME STREQUAL "iphonesimulator")
            set(_target_triple "${IOS_ARCH}-apple-ios${IOS_DEPLOYMENT_TARGET}-simulator")
        else()
            set(_target_triple "${IOS_ARCH}-apple-ios${IOS_DEPLOYMENT_TARGET}")
        endif()

        # Configure for this architecture
        execute_process(
            COMMAND sh -c
            "./Configure ${OPENSSL_TARGET} \
                 --prefix=${OPENSSL_ARCH_INSTALL_DIR} \
                 --openssldir=${OPENSSL_ARCH_INSTALL_DIR}/ssl \
                 no-shared no-tests no-legacy no-module"
            WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
            RESULT_VARIABLE CONFIGURE_RESULT
        )

        if(NOT CONFIGURE_RESULT EQUAL 0)
            message(FATAL_ERROR "OpenSSL configure failed for iOS ${IOS_ARCH}")
        endif()

        include(ProcessorCount)
        ProcessorCount(NPROC)

        # Build only libraries
        # 通过 CFLAGS 追加 -target 覆盖 OpenSSL xcrun wrapper 的默认设置，确保模拟器使用正确的 ABI
        execute_process(
            COMMAND make -j${NPROC} build_libs "CFLAGS=-target ${_target_triple}"
            WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
            RESULT_VARIABLE BUILD_RESULT
        )

        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "OpenSSL build failed for iOS ${IOS_ARCH}")
        endif()

        # Install only headers and libraries
        execute_process(
            COMMAND make install_dev
            WORKING_DIRECTORY "${OPENSSL_BUILD_DIR}"
            RESULT_VARIABLE INSTALL_RESULT
        )

        if(NOT INSTALL_RESULT EQUAL 0)
            message(FATAL_ERROR "OpenSSL install failed for iOS ${IOS_ARCH}")
        endif()

        # Repack with Apple libtool
        foreach(_lib ssl crypto)
            set(_lib_path "${OPENSSL_ARCH_INSTALL_DIR}/lib/lib${_lib}.a")

            if(EXISTS "${_lib_path}")
                set(_repack_dir "${OPENSSL_BUILD_DIR}/repack_${_lib}")
                execute_process(
                    COMMAND sh -c "rm -rf '${_repack_dir}' && mkdir -p '${_repack_dir}' \
                        && cd '${_repack_dir}' && ar -x '${_lib_path}' \
                        && libtool -static -o '${_lib_path}' *.o"
                    RESULT_VARIABLE LIBTOOL_RESULT
                )

                if(NOT LIBTOOL_RESULT EQUAL 0)
                    message(WARNING "libtool repack failed for lib${_lib}.a (${IOS_ARCH})")
                else()
                    message(STATUS "Repacked lib${_lib}.a (${IOS_ARCH}) with Apple libtool")
                endif()

                # Collect library paths for fat library creation
                if(_lib STREQUAL "ssl")
                    list(APPEND BUILT_SSL_LIBS "${_lib_path}")
                else()
                    list(APPEND BUILT_CRYPTO_LIBS "${_lib_path}")
                endif()
            endif()
        endforeach()
    endforeach()

    # Merge all per-arch libraries into the final SDK-specific install directory
    file(MAKE_DIRECTORY "${OPENSSL_INSTALL_DIR}/lib")
    file(MAKE_DIRECTORY "${OPENSSL_INSTALL_DIR}/include")

    # Copy headers from the last built arch (same for all archs)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${OPENSSL_ARCH_INSTALL_DIR}/include"
        "${OPENSSL_INSTALL_DIR}/include"
    )

    foreach(_lib ssl crypto)
        if(_lib STREQUAL "ssl")
            set(LIB_PATHS "${BUILT_SSL_LIBS}")
        else()
            set(LIB_PATHS "${BUILT_CRYPTO_LIBS}")
        endif()

        set(TARGET_LIB "${OPENSSL_INSTALL_DIR}/lib/lib${_lib}.a")

        if(NOT LIB_PATHS)
            message(WARNING "No ${_lib} libraries built")
            continue()
        endif()

        list(LENGTH LIB_PATHS LIB_COUNT)

        if(LIB_COUNT EQUAL 1)
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E copy "${LIB_PATHS}" "${TARGET_LIB}"
            )
        else()
            # lipo 合并同 SDK 下多架构（如模拟器 x86_64 + arm64）
            execute_process(
                COMMAND lipo -create ${LIB_PATHS} -output "${TARGET_LIB}"
                RESULT_VARIABLE LIPO_RESULT
            )

            if(NOT LIPO_RESULT EQUAL 0)
                message(FATAL_ERROR "lipo failed to create fat library for lib${_lib}.a")
            endif()

            message(STATUS "Created fat library: ${TARGET_LIB}")
        endif()

        execute_process(
            COMMAND lipo -info "${TARGET_LIB}"
            OUTPUT_VARIABLE LIPO_INFO
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        message(STATUS "lib${_lib}.a: ${LIPO_INFO}")
    endforeach()

    message(STATUS "OpenSSL for iOS (${IOS_SDK_NAME}) built and installed to: ${OPENSSL_INSTALL_DIR}")
endfunction()

# =============================================================================
# Android Cross-Compilation（out-of-source build）
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

    if(OPENSSL_TARGET_PLATFORM STREQUAL "android")
        if(EXISTS "${OPENSSL_INSTALL_DIR}/lib/libssl.so" OR EXISTS "${OPENSSL_INSTALL_DIR}/lib64/libssl.so")
            message(STATUS "OpenSSL already built at: ${OPENSSL_INSTALL_DIR}")
            return()
        endif()

        _openssl_build_android("${OPENSSL_SOURCE_DIR}")
    else()
        # iOS：configure 阶段同时构建 iphoneos 和 iphonesimulator 两套，
        # 确保 xcodebuild 编译真机和模拟器时都能找到对应的库。
        set(_base_dir "${CMAKE_BINARY_DIR}/deps/openssl")

        foreach(_sdk iphoneos iphonesimulator)
            if(EXISTS "${_base_dir}/${_sdk}/lib/libssl.a")
                execute_process(
                    COMMAND lipo -info "${_base_dir}/${_sdk}/lib/libssl.a"
                    OUTPUT_VARIABLE _lipo_output
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                )
                message(STATUS "OpenSSL ${_sdk} already built: ${_lipo_output}")
            else()
                message(STATUS "Building OpenSSL for ${_sdk}...")

                # 临时覆盖 OPENSSL_INSTALL_DIR 和 CMAKE_OSX_SYSROOT 以复用 _openssl_build_ios
                set(OPENSSL_INSTALL_DIR "${_base_dir}/${_sdk}")
                set(CMAKE_OSX_SYSROOT "${_sdk}")
                _openssl_build_ios("${OPENSSL_SOURCE_DIR}")
            endif()
        endforeach()

        # 恢复为当前 SDK 对应的路径
        set(OPENSSL_INSTALL_DIR "${_base_dir}/iphoneos" PARENT_SCOPE)

        if(CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
            set(OPENSSL_INSTALL_DIR "${_base_dir}/iphonesimulator" PARENT_SCOPE)
        endif()
    endif()
endfunction()

# =============================================================================
# Create CMake Target
# =============================================================================
function(create_openssl_target)
    build_openssl()

    # iOS 平台：只传 -lssl -lcrypto，不硬编码 -L 路径。
    # 库搜索路径由 xcodebuild 的 LIBRARY_SEARCH_PATHS 参数按 SDK 动态注入，
    # 确保真机用 iphoneos/lib，模拟器用 iphonesimulator/lib。
    if(IOS)
        add_library(slark::openssl INTERFACE IMPORTED GLOBAL)
        set_target_properties(slark::openssl PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL_DIR}/include"
            INTERFACE_LINK_LIBRARIES "-lssl -lcrypto"
        )
        message(STATUS "OpenSSL for iOS: -lssl -lcrypto (path resolved by xcodebuild LIBRARY_SEARCH_PATHS)")
        message(STATUS "  Include Dir: ${OPENSSL_INSTALL_DIR}/include")
        return()
    endif()

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
        # 其他平台（如 macOS）
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
