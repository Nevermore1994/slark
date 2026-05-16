macro(add_framework FRAME_WORK_NAME TARGET_NAME)
    # 使用 Xcode 的 -framework 链接方式，让 Xcode 根据当前 SDK 自动找到 framework
    target_link_libraries(${TARGET_NAME} PUBLIC "-framework ${FRAME_WORK_NAME}")
    message(STATUS "Framework ${FRAME_WORK_NAME} linked (Xcode will resolve path per-sdk)")
endmacro()

# iOS 唯一入口：创建 slark.framework，不产生任何中间 target
macro(add_ios_framework)
    set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(BASE_DIR "${ROOT_DIR}/src/base")
    set(CORE_DIR "${ROOT_DIR}/src/core")
    set(HTTP_DIR "${ROOT_DIR}/src/http")
    set(IOS_DIR "${ROOT_DIR}/src/platform/iOS")

    message(STATUS "Building for iOS: slark.framework")

    # --- 收集各模块源文件 ---
    file(GLOB_RECURSE BASE_SOURCES CONFIGURE_DEPENDS
        "${BASE_DIR}/*.cpp" "${BASE_DIR}/*.hpp" "${BASE_DIR}/*.h"
    )

    set(CORE_SOURCES "")

    if(NOT DISABLE_CORE)
        file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
            "${CORE_DIR}/*.cpp" "${CORE_DIR}/*.hpp" "${CORE_DIR}/*.h"
        )
    endif()

    set(HTTP_SOURCES "")

    if(NOT DISABLE_HTTP)
        message(STATUS "enable http")
        file(GLOB HTTP_SOURCES CONFIGURE_DEPENDS
            "${HTTP_DIR}/*.cpp" "${HTTP_DIR}/*.hpp"
        )

        # HTTPS 支持
        option(ENABLE_HTTPS "Enable HTTPS support" ON)

        if(NOT ENABLE_HTTPS)
            list(FILTER HTTP_SOURCES EXCLUDE REGEX ".*SSL.*")
            message(STATUS "HTTPS support disabled")
        else()
            message(STATUS "HTTPS support enabled")
        endif()
    else()
        message(STATUS "disable http")
    endif()

    # --- iOS 平台层：interface（对外 ObjC 接口）---
    set(IOS_INTERFACE_SOURCES
        ${IOS_DIR}/interface/SlarkPlayer.h
        ${IOS_DIR}/interface/SlarkViewController.h
        ${IOS_DIR}/interface/SlarkGraphicsConfig.h
        ${IOS_DIR}/interface/iOSUtil.h
        ${IOS_DIR}/SlarkPlayer.mm
        ${IOS_DIR}/SlarkViewController.mm
        ${IOS_DIR}/SlarkGraphicsConfig.mm
        ${IOS_DIR}/iOSUtil.mm
    )

    # --- iOS 平台层：slark_ios（排除 interface/ 文件）---
    file(GLOB_RECURSE IOS_PLATFORM_SOURCES CONFIGURE_DEPENDS
        "${IOS_DIR}/*.cpp" "${IOS_DIR}/*.hpp"
        "${IOS_DIR}/*.h" "${IOS_DIR}/*.m"
        "${IOS_DIR}/*.mm"
    )
    list(FILTER IOS_PLATFORM_SOURCES EXCLUDE REGEX ".*/interface/.*")

    foreach(_f ${IOS_INTERFACE_SOURCES})
        list(REMOVE_ITEM IOS_PLATFORM_SOURCES "${_f}")
    endforeach()

    # --- 创建 slark framework（唯一 target）---
    add_library(slark SHARED
        ${BASE_SOURCES}
        ${CORE_SOURCES}
        ${HTTP_SOURCES}
        ${IOS_INTERFACE_SOURCES}
        ${IOS_PLATFORM_SOURCES}
    )

    # --- include 路径 ---
    target_include_directories(slark PUBLIC
        "${ROOT_DIR}/include"
        "${ROOT_DIR}/internal"
        "${BASE_DIR}"
        "${BASE_DIR}/coroutine"
        "${IOS_DIR}"
        "${IOS_DIR}/interface"
    )

    if(NOT DISABLE_CORE)
        target_include_directories(slark PUBLIC
            "${CORE_DIR}"
            "${CORE_DIR}/audio"
            "${CORE_DIR}/codec"
            "${CORE_DIR}/demuxer"
            "${CORE_DIR}/pipeline"
            "${CORE_DIR}/perf"
        )
    endif()

    if(NOT DISABLE_HTTP)
        target_include_directories(slark PUBLIC "${HTTP_DIR}")
    endif()

    # --- 链接 ---
    target_link_libraries(slark PUBLIC slark_include slark_internal Threads::Threads)

    # HTTPS/OpenSSL
    if(NOT DISABLE_HTTP AND ENABLE_HTTPS)
        target_compile_definitions(slark PUBLIC ENABLE_HTTPS)

        if(EXISTS "${ROOT_DIR}/cmake/OpenSSL.cmake")
            include("${ROOT_DIR}/cmake/OpenSSL.cmake")
            target_link_openssl(slark PUBLIC)
        else()
            message(FATAL_ERROR "cmake/OpenSSL.cmake not found.")
        endif()
    endif()

    # --- framework 属性 ---
    file(GLOB_RECURSE IOS_PUBLIC_HEADERS
        "${ROOT_DIR}/include/*.h"
        "${IOS_DIR}/interface/*.h"
    )
    set_target_properties(slark PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION C
        VERSION 0.0.1
        SOVERSION 0.0.1
        MACOSX_FRAMEWORK_IDENTIFIER "com.slark.slark"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.slark.slark"
        PUBLIC_HEADER "${IOS_PUBLIC_HEADERS}"
        XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++23"
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Debug] "YES"
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH[variant=Release] "NO"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "iPhone Developer"
        XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
        XCODE_ATTRIBUTE_ALWAYS_SEARCH_USER_PATHS "NO"
    )
    target_compile_options(slark PRIVATE -Wno-missing-include-dirs)
    target_compile_definitions(slark PRIVATE GLES_SILENCE_DEPRECATION)
    target_compile_definitions(slark PUBLIC SLARK_IOS)

    add_framework(Foundation slark)
    add_framework(CoreFoundation slark)
    add_framework(AVFoundation slark)
    add_framework(VideoToolBox slark)
    add_framework(AudioToolBox slark)
    add_framework(CoreMedia slark)
    add_framework(CoreVideo slark)

    # OpenGLES/GLKit 在模拟器上不可用，仅真机链接
    if(NOT CMAKE_OSX_SYSROOT MATCHES "iphonesimulator")
        add_framework(OpenGLES slark)
        add_framework(GLKit slark)
    endif()

    add_framework(CoreGraphics slark)
    add_framework(Metal slark)
    add_framework(QuartzCore slark)
    add_framework(UIKit slark)

    # --- Xcode source group ---
    source_group(TREE "${BASE_DIR}" PREFIX "slark_base" FILES ${BASE_SOURCES})

    if(NOT DISABLE_CORE)
        source_group(TREE "${CORE_DIR}" PREFIX "slark_core" FILES ${CORE_SOURCES})
    endif()

    if(NOT DISABLE_HTTP)
        source_group(TREE "${HTTP_DIR}" PREFIX "slark_http" FILES ${HTTP_SOURCES})
    endif()

    source_group(TREE "${IOS_DIR}" PREFIX "slark_interface" FILES ${IOS_INTERFACE_SOURCES})
    source_group(TREE "${IOS_DIR}" PREFIX "slark_ios" FILES ${IOS_PLATFORM_SOURCES})

    file(GLOB_RECURSE INCLUDE_HEADERS "${ROOT_DIR}/include/*.h")
    source_group(TREE "${ROOT_DIR}/include" PREFIX "include" FILES ${INCLUDE_HEADERS})

    file(GLOB_RECURSE INTERNAL_HEADERS
        "${ROOT_DIR}/internal/*.h" "${ROOT_DIR}/internal/*.hpp"
    )
    source_group(TREE "${ROOT_DIR}/internal" PREFIX "internal" FILES ${INTERNAL_HEADERS})
endmacro()