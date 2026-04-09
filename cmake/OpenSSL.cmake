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

include_guard()
include(FetchContent)

# =============================================================================
# Configuration
# =============================================================================
set(OPENSSL_VERSION "3.0.13")
set(OPENSSL_SOURCE_URL "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz")
set(OPENSSL_MIRROR_URL "https://www.openssl.org/source/openssl-${OPENSSL_VERSION}.tar.gz")

# Cache directory for OpenSSL source (统一使用 .fetchcontent_cache 目录)
set(OPENSSL_SOURCE_CACHE_DIR "${CMAKE_SOURCE_DIR}/.fetchcontent_cache/openssl")
set(OPENSSL_INSTALL_DIR "${CMAKE_BINARY_DIR}/deps/openssl")

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

    # Check if source already exists
    if(EXISTS "${OPENSSL_SOURCE_DIR}/Configure")
        message(STATUS "OpenSSL ${OPENSSL_VERSION} source found in cache: ${OPENSSL_SOURCE_DIR}")
        set(OPENSSL_SOURCE_DIR "${OPENSSL_SOURCE_DIR}" PARENT_SCOPE)
        return()
    endif()

    message(STATUS "Downloading OpenSSL ${OPENSSL_VERSION} source...")

    # Create cache directory
    file(MAKE_DIRECTORY "${OPENSSL_SOURCE_CACHE_DIR}")

    # Try primary URL first, then mirror
    set(DOWNLOAD_URLS "${OPENSSL_SOURCE_URL}" "${OPENSSL_MIRROR_URL}")

    foreach(URL IN LISTS DOWNLOAD_URLS)
        message(STATUS "Trying to download from: ${URL}")

        execute_process(
            COMMAND ${CMAKE_COMMAND} -E echo "Downloading OpenSSL..."
            WORKING_DIRECTORY "${OPENSSL_SOURCE_CACHE_DIR}"
        )

        file(DOWNLOAD
            "${URL}"
            "${OPENSSL_SOURCE_CACHE_DIR}/openssl-${OPENSSL_VERSION}.tar.gz"
            SHOW_PROGRESS
            STATUS DOWNLOAD_STATUS
        )

        list(GET DOWNLOAD_STATUS 0 STATUS_CODE)

        if(STATUS_CODE EQUAL 0)
            # Extract the archive
            execute_process(
                COMMAND ${CMAKE_COMMAND} -E tar xzf "openssl-${OPENSSL_VERSION}.tar.gz"
                WORKING_DIRECTORY "${OPENSSL_SOURCE_CACHE_DIR}"
                RESULT_VARIABLE EXTRACT_RESULT
            )

            # Clean up archive
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
# iOS Cross-Compilation
# =============================================================================
function(_openssl_build_ios OPENSSL_SOURCE_DIR)
    message(STATUS "Building OpenSSL for iOS...")

    # Detect iOS architecture
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(IOS_ARCH "arm64")
    else()
        list(GET CMAKE_OSX_ARCHITECTURES 0 IOS_ARCH)
    endif()

    # Map to OpenSSL target
    if(IOS_ARCH STREQUAL "arm64")
        set(OPENSSL_TARGET "ios64-xcrun")
    elseif(IOS_ARCH STREQUAL "x86_64")
        set(OPENSSL_TARGET "iossimulator-xcrun")
    elseif(IOS_ARCH STREQUAL "arm64-sim")
        set(OPENSSL_TARGET "iossimulator-xcrun")
    else()
        message(FATAL_ERROR "Unsupported iOS architecture: ${IOS_ARCH}")
    endif()

    # Set minimum iOS version
    if(NOT DEFINED IOS_DEPLOYMENT_TARGET)
        set(IOS_DEPLOYMENT_TARGET "16.5")
    endif()

    # Build OpenSSL
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
        "CC=clang"
        "CFLAGS=-target ${IOS_ARCH}-apple-ios${IOS_DEPLOYMENT_TARGET} -fembed-bitcode"
        COMMAND ./Configure
        ${OPENSSL_TARGET}
        --prefix=${OPENSSL_INSTALL_DIR}
        --openssldir=${OPENSSL_INSTALL_DIR}/ssl
        no-shared
        no-tests
        no-docs
        WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
        RESULT_VARIABLE CONFIGURE_RESULT
    )

    if(NOT CONFIGURE_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenSSL configure failed for iOS")
    endif()

    # Build
    include(ProcessorCount)
    ProcessorCount(NPROC)

    execute_process(
        COMMAND make -j${NPROC}
        WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
        RESULT_VARIABLE BUILD_RESULT
    )

    if(NOT BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenSSL build failed for iOS")
    endif()

    # Install
    execute_process(
        COMMAND make install_sw
        WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
        RESULT_VARIABLE INSTALL_RESULT
    )

    if(NOT INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenSSL install failed for iOS")
    endif()

    message(STATUS "OpenSSL for iOS built and installed to: ${OPENSSL_INSTALL_DIR}")
endfunction()

# =============================================================================
# Android Cross-Compilation
# =============================================================================
function(_openssl_build_android OPENSSL_SOURCE_DIR)
    message(STATUS "Building OpenSSL for Android...")

    # Detect Android architecture
    if(NOT DEFINED ANDROID_ABI)
        set(ANDROID_ABI "arm64-v8a")
    endif()

    # Map to OpenSSL target
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

    # Find NDK toolchain
    if(NOT DEFINED ANDROID_NDK)
        if(DEFINED CMAKE_ANDROID_NDK)
            set(ANDROID_NDK "${CMAKE_ANDROID_NDK}")
        elseif(DEFINED ENV{ANDROID_NDK_HOME})
            set(ANDROID_NDK "$ENV{ANDROID_NDK_HOME}")
        else()
            message(FATAL_ERROR "ANDROID_NDK not set. Please set ANDROID_NDK or ANDROID_NDK_HOME environment variable")
        endif()
    endif()

    # Set minimum Android API level
    if(NOT DEFINED ANDROID_PLATFORM)
        set(ANDROID_PLATFORM "android-29")
    endif()

    # Extract API level
    string(REGEX REPLACE "android-([0-9]+)" "\\1" ANDROID_API_LEVEL "${ANDROID_PLATFORM}")

    # Build OpenSSL
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env
        "ANDROID_NDK_HOME=${ANDROID_NDK}"
        COMMAND ./Configure
        ${OPENSSL_TARGET}
        --prefix=${OPENSSL_INSTALL_DIR}
        --openssldir=${OPENSSL_INSTALL_DIR}/ssl
        -D__ANDROID_API__=${ANDROID_API_LEVEL}
        no-shared
        no-tests
        no-docs
        WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
        RESULT_VARIABLE CONFIGURE_RESULT
    )

    if(NOT CONFIGURE_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenSSL configure failed for Android")
    endif()

    # Build
    include(ProcessorCount)
    ProcessorCount(NPROC)

    execute_process(
        COMMAND make -j${NPROC}
        WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
        RESULT_VARIABLE BUILD_RESULT
    )

    if(NOT BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "OpenSSL build failed for Android")
    endif()

    # Install
    execute_process(
        COMMAND make install_sw
        WORKING_DIRECTORY "${OPENSSL_SOURCE_DIR}"
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

    # Download source if not cached
    _openssl_download_source()

    # Check if already built
    if(EXISTS "${OPENSSL_INSTALL_DIR}/lib/libssl.a" OR EXISTS "${OPENSSL_INSTALL_DIR}/lib64/libssl.a")
        message(STATUS "OpenSSL already built at: ${OPENSSL_INSTALL_DIR}")
        return()
    endif()

    # Build for target platform (iOS or Android only)
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
    # Build OpenSSL if not already built
    build_openssl()

    # Find library paths
    find_library(SSL_LIBRARY
        NAMES ssl libssl
        PATHS
        "${OPENSSL_INSTALL_DIR}/lib"
        "${OPENSSL_INSTALL_DIR}/lib64"
        NO_DEFAULT_PATH
    )

    find_library(CRYPTO_LIBRARY
        NAMES crypto libcrypto
        PATHS
        "${OPENSSL_INSTALL_DIR}/lib"
        "${OPENSSL_INSTALL_DIR}/lib64"
        NO_DEFAULT_PATH
    )

    if(NOT SSL_LIBRARY OR NOT CRYPTO_LIBRARY)
        message(FATAL_ERROR "OpenSSL libraries not found after build")
    endif()

    # Create imported target
    add_library(slark::openssl STATIC IMPORTED GLOBAL)
    set_target_properties(slark::openssl PROPERTIES
        IMPORTED_LOCATION "${SSL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INSTALL_DIR}/include"
        INTERFACE_LINK_LIBRARIES "${CRYPTO_LIBRARY}"
    )

    # Platform-specific link libraries
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_property(TARGET slark::openssl APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES dl pthread
        )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # iOS specific frameworks if needed
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
    target_compile_definitions(${TARGET} ${ARGN} ENABLE_HTTPS)
endfunction()
