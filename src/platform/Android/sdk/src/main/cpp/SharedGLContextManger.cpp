//
// Created by Nevermore- on 2025/5/12.
//

#include "JNIEnvGuard.hpp"
#include "JNIHelper.h"
#include "JNISignature.h"
#include "AndroidEGLContext.h"
#include "Manager.hpp"
#include "Log.hpp"
#include "GLContextManager.h"
#include <android/native_window_jni.h>
#include "VideoRender.h"

namespace slark {

struct SharedEGLContextInfo {
    std::string playerId;
    std::shared_ptr<AndroidEGLContext> context;
    EGLSurface surface = nullptr;
};

using SharedEGLContextManager = Manager<SharedEGLContextInfo>;

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_slark_sdk_EGLRenderThread_createEGLContext(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring player_id,
    jobject surface
) {
    auto* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        LogE("failed to get ANativeWindow from surface");
        return false;
    }
    auto playerId = JNI::FromJVM::toString(env, player_id);
    auto mainContext = GLContextManager::shareInstance().getMainContext(playerId);
    if (mainContext == nullptr) {
        LogE("failed to get main context for playerId: {}", playerId);
        return false;
    }
    auto eglContext = std::make_shared<AndroidEGLContext>();
    if (!eglContext->init(mainContext->nativeContext())) {
        LogE("failed to create EGL context");
        return false;
    }
    auto eglSurface = eglContext->createWindowSurface(window);
    if (eglSurface == nullptr) {
        LogE("failed to create EGL surface");
        return false;
    }
    eglContext->attachContext(eglSurface);
    ANativeWindow_release(window);
    auto isSuccess = eglGetError() == 0;
    if (isSuccess) {
        auto info = std::make_shared<SharedEGLContextInfo>();
        info->playerId = playerId;
        info->context = eglContext;
        info->surface = eglSurface;
        SharedEGLContextManager::shareInstance().add(playerId, std::move(info));
    } else {
        LogE("failed to attach EGL context");
    }
    return isSuccess;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_EGLRenderThread_destroyEGLContext(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring player_id
) {
    auto playerId = JNI::FromJVM::toString(env, player_id);
    auto contextInfo = SharedEGLContextManager::shareInstance().find(playerId);
    if (contextInfo == nullptr) {
        LogE("failed to find EGL context for playerId: {}", playerId);
        return;
    }
    auto eglContext = contextInfo->context;
    if (eglContext == nullptr) {
        LogE("failed to get EGL context for playerId: {}", playerId);
        return;
    }
    contextInfo->context->releaseSurface(contextInfo->surface);
    contextInfo->surface = nullptr;
    eglContext->detachContext();
    SharedEGLContextManager::shareInstance().remove(playerId);
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_slark_sdk_EGLRenderThread_swapGLBuffers(
        JNIEnv *env,
        jobject /*thiz*/,
        jstring player_id,
        jint textureId
) {
    auto playerId = JNI::FromJVM::toString(env, player_id);
    auto contextInfo = SharedEGLContextManager::shareInstance().find(playerId);
    if (contextInfo == nullptr) {
        LogE("failed to find EGL context for playerId: {}", playerId);
        return false;
    }
    auto eglContext = contextInfo->context;
    if (eglContext == nullptr) {
        LogE("failed to get EGL context for playerId: {}", playerId);
        return false;
    }
    auto display = eglContext->getDisplay();
    if (display == nullptr) {
        LogE("failed to get EGL display for playerId: {}", playerId);
        return false;
    }
    auto result = eglSwapBuffers(display, contextInfo->surface);
    if (result == EGL_FALSE) {
        LogE("failed to swap buffers for playerId: {} {}", playerId, eglGetError());
    }
    if (textureId != 0) {
        auto render = VideoRenderManager::shareInstance().find(playerId);
        if (render) {
            render->renderComplete(textureId);
        } else {
            LogE("failed to find player for playerId: {}", playerId);
        }
    }
    return result == EGL_TRUE;
}

} // slark
