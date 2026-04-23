set(BASE_FILE_LISTS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/coroutine/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/coroutine/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/coroutine/*.cpp
)
file(GLOB BASE_FILES CONFIGURE_DEPENDS ${BASE_FILE_LISTS})

add_library(slark_base OBJECT ${BASE_FILES})

target_include_directories(slark_base
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/coroutine
    ${CMAKE_CURRENT_SOURCE_DIR}/../../internal
)

target_link_libraries(slark_base PUBLIC Threads::Threads)
