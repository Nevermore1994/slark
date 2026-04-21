//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "JNIBase.h"
#include "JNICache.h"
#include "utils/Log.h"

namespace slark::JNI {

namespace {
JavaVM* g_javaVM = nullptr;
jobject g_applicationContext = nullptr;

void loadClassCache(JNIEnv* env) {
    static const char* kClasses[] = {
        "com/slark/sdk/SlarkNativeBridge",
        "com/slark/sdk/SlarkPlayerManager",
        "com/slark/api/SlarkPlayerState",
        "com/slark/api/SlarkPlayerEvent",
        "com/slark/sdk/MediaCodecDecoder$Action",
        "com/slark/sdk/MediaCodecDecoder",
        "com/slark/sdk/AudioPlayer",
        "com/slark/sdk/AudioPlayer$Action",
        "com/slark/sdk/AudioPlayer$Config",
        "com/slark/sdk/RenderTexture",
    };

    for (const auto* className : kClasses) {
        if (!JNICache::instance().getClass(env, className)) {
            FLOGE("Failed to find class: {}", className);
        }
    }
}

} // namespace

namespace

    JavaVM*
    getJavaVM() noexcept {
    return g_javaVM;
}

jobject getApplicationContext() noexcept {
    return g_applicationContext;
}

void setApplicationContext(JNIEnv* env, jobject context) noexcept {
    if (!env || !context) {
        return;
    }
    if (g_applicationContext) {
        env->DeleteGlobalRef(g_applicationContext);
    }
    g_applicationContext = env->NewGlobalRef(context);
}

} // namespace slark::JNI

namespace slark::JNI

    extern "C" JNIEXPORT jint JNICALL
    JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
    slark::JNI::g_javaVM = vm;
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        FLOGE("Failed to get JNI environment");
        return -1;
    }
    slark::JNI::loadClassCache(env);
    return JNI_VERSION_1_6;
} // namespace slark::JNI

extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* /* reserved */) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        if (slark::JNI::g_applicationContext) {
            env->DeleteGlobalRef(slark::JNI::g_applicationContext);
            slark::JNI::g_applicationContext = nullptr;
        }
    }
    slark::JNI::g_javaVM = nullptr;
    slark::JNI::JNICache::instance().clear();
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkNativeBridge_00024Companion_setContext(JNIEnv* env,
                                                               jobject /*thiz*/,
                                                               jobject context) {
    slark::JNI::setApplicationContext(env, context);
}
