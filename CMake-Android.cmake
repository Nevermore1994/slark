
macro(add_android_sources TARGET_NAME)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -frtti")
    find_library(ANDROID_LOG_LIB log)
    find_library(ANDROID_LIB android)
    find_library(ANDROID_GLESv3_LIB GLESv3)
    find_library(ANDROID_EGL_LIB EGL)

    set(ANDROID_SOURCE_FILES src/platform/Android/*.cpp)
    file(GLOB Android_FILES ${ANDROID_SOURCE_FILES})
    target_sources(${TARGET_NAME} PUBLIC ${Android_FILES})
    target_include_directories(${TARGET_NAME} PUBLIC src/interface/Android)
    target_include_directories(${TARGET_NAME} PRIVATE src/platform/Android)
    target_include_directories(${TARGET_NAME} PUBLIC ${ANDROID_NDK_ROOT}/sources/android/native_app_glue)

    target_compile_definitions(${TARGET_NAME}
        PUBLIC
        ANDROID_PLATFORM
        __ANDROID__
        SLARK_ANDROID
    )

    target_link_libraries(${TARGET_NAME}
            PUBLIC
            ${ANDROID_LOG_LIB}
            ${ANDROID_LIB}
            ${ANDROID_GLESv3_LIB}
            ${ANDROID_EGL_LIB}
            )
endmacro()
