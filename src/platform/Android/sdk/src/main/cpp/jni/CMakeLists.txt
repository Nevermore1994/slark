add_library(slark_jni_common STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/JNICache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/JNIFileUtil.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/JNIInit.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/JNIMarshal.cpp
)

target_include_directories(slark_jni_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(slark_jni_common PRIVATE -std=c++23)

target_compile_definitions(slark_jni_common PUBLIC
    __ANDROID__
)

target_link_libraries(slark_jni_common PUBLIC slark android log)
