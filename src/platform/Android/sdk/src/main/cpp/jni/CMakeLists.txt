file(GLOB JNI_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
)

add_library(slark_jni_common STATIC ${JNI_COMMON_SOURCES})

target_include_directories(slark_jni_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SLARK_PROJECT_PATH}/src/base
    ${SLARK_PROJECT_PATH}/include
    ${SLARK_PROJECT_PATH}/internal
)

target_compile_options(slark_jni_common PRIVATE -std=c++23)

target_compile_definitions(slark_jni_common PUBLIC
    __ANDROID__
)

target_link_libraries(slark_jni_common PUBLIC android log)
