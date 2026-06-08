
macro(add_android_interface)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions -frtti")
    find_library(ANDROID_LOG_LIB log)
    find_library(ANDROID_LIB android)
    find_library(ANDROID_GLESv3_LIB GLESv3)
    find_library(ANDROID_EGL_LIB EGL)

    # 产出 slark_jni (cpp 目录下的业务代码)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/platform/Android/sdk/src/main/cpp)

    # 产出 slark_jni_common (JNI 通用层)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/platform/Android/sdk/src/main/cpp/jni ${CMAKE_CURRENT_BINARY_DIR}/slark_jni_common)
endmacro()
