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

add_library(${MODULE_NAME} ${HTTP_SOURCES} ${HTTP_HEADERS})

target_include_directories(${MODULE_NAME}
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${MODULE_NAME} PUBLIC slark_base Threads::Threads)

# HTTPS/OpenSSL 支持
if(ENABLE_HTTPS)
    target_compile_definitions(${MODULE_NAME} PUBLIC ENABLE_HTTPS)

    find_package(OpenSSL QUIET)

    if(OpenSSL_FOUND)
        target_link_libraries(${MODULE_NAME} PUBLIC OpenSSL::SSL OpenSSL::Crypto)
        message(STATUS "Using system OpenSSL: ${OPENSSL_VERSION}")
    else()
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