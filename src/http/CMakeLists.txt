set(MODULE_NAME slark_http)

# 自动收集所有源文件和头文件
file(GLOB HTTP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB HTTP_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")

# HTTPS 支持选项
option(ENABLE_HTTPS "Enable HTTPS support" ON)

if(NOT ENABLE_HTTPS)
    list(FILTER HTTP_SOURCES EXCLUDE REGEX ".*SSL.*")
    list(FILTER HTTP_HEADERS EXCLUDE REGEX ".*SSL.*")
    list(FILTER HTTP_HEADERS EXCLUDE REGEX ".*Https.*")
    message(STATUS "HTTPS support disabled")
else()
    message(STATUS "HTTPS support enabled")
endif()

add_library(${MODULE_NAME} OBJECT ${HTTP_SOURCES} ${HTTP_HEADERS})

target_include_directories(${MODULE_NAME}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${MODULE_NAME} PUBLIC slark_base Threads::Threads)

# HTTPS/OpenSSL 支持
if(ENABLE_HTTPS)
    target_compile_definitions(${MODULE_NAME} PUBLIC ENABLE_HTTPS)

    # iOS/Android 必须从源码编译对应平台的 OpenSSL，不能使用宿主机系统库
    # - Android: 动态库（.so），打入 AAR
    # - iOS:     静态库（.a），静态链接进 framework
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/OpenSSL.cmake")
            include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/OpenSSL.cmake)
            target_link_openssl(${MODULE_NAME} PUBLIC)
        else()
            message(FATAL_ERROR "cmake/OpenSSL.cmake not found. Required for iOS/Android HTTPS support.")
        endif()
    else()
        # 桌面平台使用系统 OpenSSL
        find_package(OpenSSL REQUIRED)
        target_link_libraries(${MODULE_NAME} PUBLIC OpenSSL::SSL OpenSSL::Crypto)
        message(STATUS "Using system OpenSSL: ${OPENSSL_VERSION}")
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