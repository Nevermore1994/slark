cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MODULE_NAME slark_http)

# 自动收集所有源文件和头文件
file(GLOB HTTP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cc")
file(GLOB HTTP_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")

# HTTPS 支持选项
option(ENABLE_HTTPS "Enable HTTPS support" ON)

if(NOT ENABLE_HTTPS)
    # 排除 SSL 相关文件
    list(FILTER HTTP_SOURCES EXCLUDE REGEX ".*SSL.*")
    list(FILTER HTTP_HEADERS EXCLUDE REGEX ".*SSL.*")
    list(FILTER HTTP_HEADERS EXCLUDE REGEX ".*Https.*")
    message(STATUS "HTTPS support disabled")
else()
    message(STATUS "HTTPS support enabled")
endif()

# 创建库目标
add_library(${MODULE_NAME} ${HTTP_SOURCES} ${HTTP_HEADERS})

# 设置包含目录
target_include_directories(${MODULE_NAME}
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/slark/http>
)

# 链接基础库
if(TARGET slark_base)
    # 作为 slark 子模块使用
    target_link_libraries(${MODULE_NAME} PUBLIC slark_base Threads::Threads)
else()
    # 独立模块编译：需要提供 base 组件路径
    set(SLARK_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../base" CACHE PATH "Path to slark base directory")

    if(NOT EXISTS "${SLARK_BASE_DIR}/Data.hpp")
        message(FATAL_ERROR "slark_base not found at ${SLARK_BASE_DIR}. Set SLARK_BASE_DIR to the correct path.")
    endif()

    # 包含 base 目录
    target_include_directories(${MODULE_NAME} PUBLIC
        ${SLARK_BASE_DIR}
        ${SLARK_BASE_DIR}/coroutine
        ${SLARK_BASE_DIR}/../include
    )

    # 收集 base 源文件
    file(GLOB BASE_SOURCES
        "${SLARK_BASE_DIR}/*.cc"
        "${SLARK_BASE_DIR}/*.hpp"
        "${SLARK_BASE_DIR}/*.h"
        "${SLARK_BASE_DIR}/coroutine/*.cc"
        "${SLARK_BASE_DIR}/coroutine/*.hpp"
    )

    # 创建 base 静态库
    add_library(slark_base_static STATIC ${BASE_SOURCES})
    target_include_directories(slark_base_static PUBLIC
        ${SLARK_BASE_DIR}
        ${SLARK_BASE_DIR}/coroutine
        ${SLARK_BASE_DIR}/../include
    )
    target_link_libraries(slark_base_static PUBLIC Threads::Threads)

    target_link_libraries(${MODULE_NAME} PUBLIC slark_base_static Threads::Threads)
endif()

# HTTPS/OpenSSL 支持
if(ENABLE_HTTPS)
    target_compile_definitions(${MODULE_NAME} PUBLIC ENABLE_HTTPS)

    # 查找 OpenSSL
    find_package(OpenSSL QUIET)

    if(OpenSSL_FOUND)
        target_link_libraries(${MODULE_NAME} PUBLIC OpenSSL::SSL OpenSSL::Crypto)
        message(STATUS "Using system OpenSSL: ${OPENSSL_VERSION}")
    else()
        # 尝试使用项目内的 OpenSSL 配置
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/OpenSSL.cmake")
            include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/OpenSSL.cmake)
            target_link_openssl(${MODULE_NAME} PUBLIC)
        else()
            message(WARNING "OpenSSL not found. HTTPS support may not work.")
        endif()
    endif()
endif()

# 平台特定设置
if(DEFINED BUILD_PLATFORM)
    if(BUILD_PLATFORM STREQUAL "iOS")
        target_compile_definitions(${MODULE_NAME} PUBLIC SLARK_IOS)
    elseif(BUILD_PLATFORM STREQUAL "Android")
        target_compile_definitions(${MODULE_NAME} PUBLIC SLARK_ANDROID)
    endif()
endif()

# 安装配置（独立编译时）
if(NOT TARGET slark)
    include(GNUInstallDirs)

    install(TARGETS ${MODULE_NAME}
        EXPORT slark_httpTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(FILES ${HTTP_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/slark/http
    )

    # 生成 CMake 配置文件
    install(EXPORT slark_httpTargets
        FILE slark_httpTargets.cmake
        NAMESPACE slark::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/slark_http
    )
endif()

# 测试（可选）
option(BUILD_HTTP_TESTS "Build HTTP module tests" OFF)

if(BUILD_HTTP_TESTS AND NOT DISABLE_TEST)
    enable_testing()
    add_subdirectory(tests)
endif()