slark_collect_sources(CORE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(slark_core OBJECT ${CORE_FILES})

target_include_directories(slark_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/source
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/pipeline)

target_include_directories(slark_core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/media
    ${CMAKE_CURRENT_SOURCE_DIR}/stream
    ${CMAKE_CURRENT_SOURCE_DIR}/render
    ${CMAKE_CURRENT_SOURCE_DIR}/demuxer
    ${CMAKE_CURRENT_SOURCE_DIR}/codec
    ${CMAKE_CURRENT_SOURCE_DIR}/../perf
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/pipeline
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/source
)

# 需要包含Android平台特有的头文件
if(SLARK_ANDROID)
    target_include_directories(slark_core
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/Android/sdk/src/main/cpp
    )
endif()

target_link_libraries(slark_core PUBLIC slark_base slark_internal slark_include slark_perf)

if(NOT DISABLE_HTTP)
    target_link_libraries(slark_core PUBLIC slark_http)
endif()
