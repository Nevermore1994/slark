slark_collect_sources(CORE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")

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
