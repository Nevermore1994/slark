cmake_minimum_required(VERSION 3.22.1)

project("sdk")

set(SLARK_PROJECT_PATH ${CMAKE_SOURCE_DIR}/../../../../../../..)

message(STATUS ${SLARK_PROJECT_PATH})

# add slark library
add_subdirectory(${SLARK_PROJECT_PATH} slark)

# add jni common library
add_subdirectory(jni)

set(BUSINESS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/AACHardwareDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AndroidEGLContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AudioPlayerNative.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AudioRender.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FrameBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FrameBufferPool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/NativeDecoderManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/NativeHardwareDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PlayerManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/RenderManger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SlarkLogJNI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SlarkNative.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TexturePool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoDecodeResource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoHardwareDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/VideoRender.cpp
)

add_library(${CMAKE_PROJECT_NAME} SHARED ${BUSINESS_SOURCES})

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    .
    ${SLARK_PROJECT_PATH}/src/core
    ${SLARK_PROJECT_PATH}/src/core/codec
    ${SLARK_PROJECT_PATH}/src/core/audio
    ${SLARK_PROJECT_PATH}/internal
)

target_link_libraries(${CMAKE_PROJECT_NAME}
    PRIVATE

    slark
    slark_jni_common
)