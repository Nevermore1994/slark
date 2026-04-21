set(SLARK_PLATFORM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../platform)

set(CORE_FILE_LISTS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/demuxer/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/demuxer/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/demuxer/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/codec/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/codec/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/codec/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/codec/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/perf/*.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/perf/*.hpp
)

if(APPLE)
    list(APPEND CORE_FILE_LISTS
        ${SLARK_PLATFORM_DIR}/iOS/AACHardwareDecoder.mm
        ${SLARK_PLATFORM_DIR}/iOS/VideoHardwareDecoder.mm
    )
endif()

file(GLOB CORE_FILES CONFIGURE_DEPENDS ${CORE_FILE_LISTS})

add_library(slark_core OBJECT ${CORE_FILES})

target_include_directories(slark_core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/demuxer
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/codec
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline
    $<$<BOOL:${APPLE}>:${SLARK_PLATFORM_DIR}/iOS>
)

target_link_libraries(slark_core PUBLIC slark_base slark_internal slark_include)
