slark_collect_sources(PERF_FILES "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(slark_perf OBJECT ${PERF_FILES})

target_include_directories(slark_perf
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(slark_perf PUBLIC slark_base slark_internal slark_include)
