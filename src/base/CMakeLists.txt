slark_collect_sources(BASE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(slark_base OBJECT ${BASE_FILES})

target_include_directories(slark_base
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/coroutine
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../internal
)

target_link_libraries(slark_base PUBLIC Threads::Threads)
