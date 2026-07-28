//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "JNIBase.h"
#include "JNICache.h"
#include "JNIMarshal.h"
#include "Log.hpp"
#include "LogConfig.h"

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
        "com/slark/sdk/RenderTexture",
        "com/slark/sdk/AudioDecoderConfig",
        "com/slark/sdk/VideoDecoderConfig",
        "com/slark/sdk/DecodeMode",
    };

    for (const auto* className : kClasses) {
        if (!JNICache::instance().getClass(env, className)) {
            LogE("Failed to find class: {}", className);
        }
    }
}

}  // namespace

JavaVM* getJavaVM() noexcept {
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

}  // namespace slark::JNI

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
    using namespace slark;
    slark::JNI::g_javaVM = vm;
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        LogE("Failed to get JNI environment");
        return -1;
    }
    slark::JNI::loadClassCache(env);
    return JNI_VERSION_1_6;
}

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

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkNativeBridge_00024Companion_setLogConfig(JNIEnv* env,
                                                                 jobject /*thiz*/,
                                                                 jlong progressLogBytes,
                                                                 jlong maxLogFileSize,
                                                                 jint maxLogFileCount,
                                                                 jint minLogLevel,
                                                                 jstring logDir,
                                                                 jstring logFileName) {
    slark::LogConfig config;
    config.progressLogBytes = static_cast<uint64_t>(progressLogBytes);
    config.maxLogFileSize = static_cast<uint64_t>(maxLogFileSize);
    config.maxLogFileCount = static_cast<uint32_t>(maxLogFileCount);
    config.minLogLevel = minLogLevel;
    if (logDir) {
        config.logDir = slark::JNI::FromJVM::str(env, logDir);
    }
    if (logFileName) {
        config.logFileName = slark::JNI::FromJVM::str(env, logFileName);
    }
    slark::setLogConfig(config);
}
