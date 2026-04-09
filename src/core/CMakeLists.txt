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
    ${CMAKE_CURRENT_SOURCE_DIR}/video/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/public/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/public/*.h
)
file(GLOB CORE_FILES ${CORE_FILE_LISTS})

add_library(slark_core OBJECT ${CORE_FILES})

target_include_directories(slark_core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/public
    ${CMAKE_CURRENT_SOURCE_DIR}/demuxer
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/codec
    ${CMAKE_CURRENT_SOURCE_DIR}/video
)

target_link_libraries(slark_core PUBLIC slark_base)
