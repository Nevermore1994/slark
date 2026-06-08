set(SLARK_PLATFORM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../platform)

set(CORE_FILE_LISTS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/demuxer/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/demuxer/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/demuxer/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/codec/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/codec/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/codec/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/perf/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/perf/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/perf/*.h
)

file(GLOB CORE_FILES CONFIGURE_DEPENDS ${CORE_FILE_LISTS})

add_library(slark_core OBJECT ${CORE_FILES})

target_include_directories(slark_core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/demuxer
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/codec
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/perf
)

# 需要包含Android平台特有的头文件
if(SLARK_ANDROID)
    target_include_directories(slark_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/Android/sdk/src/main/cpp
    )
endif()

target_link_libraries(slark_core PUBLIC slark_base slark_internal slark_include)

if(NOT DISABLE_HTTP)
    target_link_libraries(slark_core PUBLIC slark_http)
endif()
