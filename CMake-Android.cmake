
macro(add_android_sources TARGET_NAME)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -frtti")
    find_library(ANDROID_LOG_LIB log)
    find_library(ANDROID_LIB android)
    find_library(ANDROID_GLESv3_LIB GLESv3)
    find_library(ANDROID_EGL_LIB EGL)

    set(ANDROID_SOURCE_FILES
        src/platform/Android/sdk/src/main/cpp/AACHardwareDecoder.cc
        src/platform/Android/sdk/src/main/cpp/AndroidEGLContext.cpp
        src/platform/Android/sdk/src/main/cpp/AudioPlayerNative.cpp
        src/platform/Android/sdk/src/main/cpp/AudioRender.cpp
        src/platform/Android/sdk/src/main/cpp/FrameBuffer.cpp
        src/platform/Android/sdk/src/main/cpp/FrameBufferPool.cpp
        src/platform/Android/sdk/src/main/cpp/NativeDecoderManager.cpp
        src/platform/Android/sdk/src/main/cpp/NativeHardwareDecoder.cpp
        src/platform/Android/sdk/src/main/cpp/PlayerManager.cpp
        src/platform/Android/sdk/src/main/cpp/RenderManger.cpp
        src/platform/Android/sdk/src/main/cpp/SlarkLogJNI.cpp
        src/platform/Android/sdk/src/main/cpp/SlarkNative.cpp
        src/platform/Android/sdk/src/main/cpp/Texture.cpp
        src/platform/Android/sdk/src/main/cpp/TexturePool.cpp
        src/platform/Android/sdk/src/main/cpp/VideoDecodeResource.cpp
        src/platform/Android/sdk/src/main/cpp/VideoHardwareDecoder.cc
        src/platform/Android/sdk/src/main/cpp/VideoRender.cpp
    )
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/platform/Android/sdk/src/main/cpp/jni ${CMAKE_CURRENT_BINARY_DIR}/slark_jni_common_android)
    target_sources(${TARGET_NAME} PUBLIC ${ANDROID_SOURCE_FILES})
    target_include_directories(${TARGET_NAME} PRIVATE src/platform/Android/sdk/src/main/cpp/)
    target_include_directories(${TARGET_NAME} PUBLIC src/platform/Android/sdk/src/main/cpp/jni ${ANDROID_NDK_ROOT}/sources/android/native_app_glue)

    target_compile_definitions(${TARGET_NAME}
        PUBLIC
        ANDROID_PLATFORM
        __ANDROID__
        SLARK_ANDROID
    )

    target_link_libraries(${TARGET_NAME}
        PUBLIC
        slark_jni_common
        ${ANDROID_LOG_LIB}
        ${ANDROID_LIB}
        ${ANDROID_GLESv3_LIB}
        ${ANDROID_EGL_LIB}
    )
endmacro()
