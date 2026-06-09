//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AndroidEGLContext.h"
#include "AndroidVulkanContext.h"
#include "GLContextManager.h"
#include "JNICache.h"
#include "JNICall.h"
#include "JNIEnvGuard.h"
#include "JNIMarshal.h"
#include "JNISignature.h"
#include "Log.hpp"
#include "Manager.hpp"
#include "VideoRender.h"

#include <android/native_window_jni.h>

namespace slark {

struct SharedEGLContextInfo {
    std::string playerId;
    std::shared_ptr<AndroidEGLContext> context;
    EGLSurface surface = nullptr;
};

using SharedEGLContextManager = Manager<SharedEGLContextInfo>;

using VulkanContextManager = Manager<AndroidVulkanContext>;

static void RenderManager_rebuildSurface(JNIEnv* env,
                                         const std::string& playerId,
                                         jobject surface) {
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
    eglContext->releaseSurface(contextInfo->surface);
    contextInfo->surface = nullptr;
    eglContext->detachContext();

    auto* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        LogE("failed to get ANativeWindow from surface");
        return;
    }
    auto eglSurface = eglContext->createWindowSurface(window);
    if (eglSurface == nullptr) {
        LogE("failed to create EGL surface for playerId: {}", playerId);
        ANativeWindow_release(window);
        return;
    }
    eglContext->attachContext(eglSurface);
    contextInfo->surface = eglSurface;
    ANativeWindow_release(window);
}

static jboolean RenderManager_createEGLContext(JNIEnv* env,
                                               const std::string& playerId,
                                               jobject surface) {
    auto* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        LogE("failed to get ANativeWindow from surface");
        return false;
    }
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
    auto error = eglGetError();
    if (error != EGL_SUCCESS) {
        LogE("failed to attach EGL context to surface, error: {}", error);
        return false;
    }
    auto info = std::make_shared<SharedEGLContextInfo>();
    info->playerId = playerId;
    info->context = eglContext;
    info->surface = eglSurface;
    SharedEGLContextManager::shareInstance().add(playerId, std::move(info));
    return true;
}

static void RenderManager_destroyEGLContext(const std::string& playerId) {
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

static jboolean RenderManager_swapGLBuffers(const std::string& playerId, jint textureId) {
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
        LogE("failed to swap buffers for playerId: {} error: {}", playerId, eglGetError());
    }
    if (textureId != 0) {
        auto render = VideoRenderManager::shareInstance().find(playerId);
        if (render) {
            render->renderFrameComplete(textureId);
        } else {
            LogE("failed to find player for playerId: {}", playerId);
        }
    }
    return result == EGL_TRUE;
}

static jobject RenderManager_getBackupTextureId(JNIEnv* env, const std::string& playerId) {
    static std::string_view kTextureClass = "com/slark/sdk/RenderTexture";
    auto render = VideoRenderManager::shareInstance().find(playerId);
    if (!render) {
        LogE("failed to find player for playerId: {}", playerId);
        return nullptr;
    }
    auto texture = render->getBackupTexture();
    if (!texture || !texture->isValid()) {
        LogE("backup texture is null for playerId: {}", playerId);
        return nullptr;
    }
    return JNI::JNICall::create(env,
                                kTextureClass,
                                static_cast<int32_t>(texture->textureId()),
                                static_cast<int32_t>(texture->width()),
                                static_cast<int32_t>(texture->height()))
        .detach();
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_EGLRenderThread_rebuildSurface(JNIEnv* env,
                                                  jobject /*thiz*/,
                                                  jstring player_id,
                                                  jobject surface) {
    RenderManager_rebuildSurface(env, JNI::FromJVM::str(env, player_id), surface);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slark_sdk_EGLRenderThread_createEGLContext(JNIEnv* env,
                                                    jobject /*thiz*/,
                                                    jstring player_id,
                                                    jobject surface) {
    return RenderManager_createEGLContext(env, JNI::FromJVM::str(env, player_id), surface);
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_EGLRenderThread_destroyEGLContext(JNIEnv* env,
                                                     jobject /*thiz*/,
                                                     jstring player_id) {
    RenderManager_destroyEGLContext(JNI::FromJVM::str(env, player_id));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slark_sdk_EGLRenderThread_swapGLBuffers(JNIEnv* env,
                                                 jobject /*thiz*/,
                                                 jstring player_id,
                                                 jint textureId) {
    return RenderManager_swapGLBuffers(JNI::FromJVM::str(env, player_id), textureId);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_slark_sdk_EGLRenderThread_getBackupTextureId(JNIEnv* env,
                                                      jobject /*thiz*/,
                                                      jstring player_id) {
    return RenderManager_getBackupTextureId(env, JNI::FromJVM::str(env, player_id));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slark_sdk_GLRenderThread_createEGLContext(JNIEnv* env,
                                                   jobject /*thiz*/,
                                                   jstring player_id,
                                                   jobject surface) {
    return RenderManager_createEGLContext(env, JNI::FromJVM::str(env, player_id), surface);
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_GLRenderThread_destroyEGLContext(JNIEnv* env,
                                                    jobject /*thiz*/,
                                                    jstring player_id) {
    RenderManager_destroyEGLContext(JNI::FromJVM::str(env, player_id));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slark_sdk_GLRenderThread_swapGLBuffers(JNIEnv* env,
                                                jobject /*thiz*/,
                                                jstring player_id,
                                                jint textureId) {
    return RenderManager_swapGLBuffers(JNI::FromJVM::str(env, player_id), textureId);
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_slark_sdk_GLRenderThread_getBackupTextureId(JNIEnv* env,
                                                     jobject /*thiz*/,
                                                     jstring player_id) {
    return RenderManager_getBackupTextureId(env, JNI::FromJVM::str(env, player_id));
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_GLRenderThread_rebuildSurface(JNIEnv* env,
                                                 jobject /*thiz*/,
                                                 jstring player_id,
                                                 jobject surface) {
    RenderManager_rebuildSurface(env, JNI::FromJVM::str(env, player_id), surface);
}

/// Choreographer VSync 回调入口：通知 C++ VideoRender 拉取并渲染下一帧
extern "C" JNIEXPORT void JNICALL Java_com_slark_sdk_GLRenderThread_onVSync(JNIEnv* env,
                                                                            jobject /*thiz*/,
                                                                            jstring player_id) {
    auto playerId = JNI::FromJVM::str(env, player_id);
    auto render = VideoRenderManager::shareInstance().find(playerId);
    if (render) {
        render->onVSync();
    } else {
        LogE("onVSync[GL]: failed to find VideoRender for playerId:{}", playerId);
    }
}

extern "C" JNIEXPORT void JNICALL Java_com_slark_sdk_VulkanRenderThread_onVSync(JNIEnv* env,
                                                                                jobject /*thiz*/,
                                                                                jstring player_id) {
    auto playerId = JNI::FromJVM::str(env, player_id);
    auto render = VideoRenderManager::shareInstance().find(playerId);
    if (render) {
        render->onVSync();
    } else {
        LogE("onVSync[VK]: failed to find VideoRender for playerId:{}", playerId);
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slark_sdk_VulkanRenderThread_createVulkanContext(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jstring player_id,
                                                          jobject surface,
                                                          jint width,
                                                          jint height) {
    auto playerId = JNI::FromJVM::str(env, player_id);
    auto* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        LogE("createVulkanContext: failed to get ANativeWindow for playerId:{}", playerId);
        return false;
    }
    // 获取主 EGL 上下文，使 Vulkan 的 EGL interop 上下文与解码器共享纹理
    auto mainContext = GLContextManager::shareInstance().getMainContext(playerId);
    void* nativeSharedCtx = mainContext ? mainContext->nativeContext() : nullptr;
    auto ctx = std::make_shared<AndroidVulkanContext>();
    bool ok = ctx->init(window, width, height, nativeSharedCtx);
    ANativeWindow_release(window);
    if (!ok) {
        LogE("createVulkanContext: init failed for playerId:{}", playerId);
        return false;
    }
    VulkanContextManager::shareInstance().add(playerId, std::move(ctx));
    return true;
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_VulkanRenderThread_destroyVulkanContext(JNIEnv* env,
                                                           jobject /*thiz*/,
                                                           jstring player_id) {
    auto playerId = JNI::FromJVM::str(env, player_id);
    auto ctx = VulkanContextManager::shareInstance().find(playerId);
    if (ctx) {
        ctx->release();
    }
    VulkanContextManager::shareInstance().remove(playerId);
}

extern "C" JNIEXPORT void JNICALL Java_com_slark_sdk_VulkanRenderThread_drawFrame(JNIEnv* env,
                                                                                  jobject /*thiz*/,
                                                                                  jstring player_id,
                                                                                  jint textureId,
                                                                                  jint width,
                                                                                  jint height) {
    auto ctx = VulkanContextManager::shareInstance().find(JNI::FromJVM::str(env, player_id));
    if (ctx) {
        ctx->drawFrame(textureId, width, height);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_VulkanRenderThread_presentVulkanFrame(JNIEnv* env,
                                                         jobject /*thiz*/,
                                                         jstring player_id,
                                                         jint textureId) {
    auto render = VideoRenderManager::shareInstance().find(JNI::FromJVM::str(env, player_id));
    if (render) {
        render->renderFrameComplete(textureId);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_VulkanRenderThread_renderFrameComplete(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jstring player_id,
                                                          jint textureId) {
    auto render = VideoRenderManager::shareInstance().find(JNI::FromJVM::str(env, player_id));
    if (render) {
        render->renderFrameComplete(textureId);
    }
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_slark_sdk_VulkanRenderThread_getBackupTextureId(JNIEnv* env,
                                                         jobject /*thiz*/,
                                                         jstring player_id) {
    return RenderManager_getBackupTextureId(env, JNI::FromJVM::str(env, player_id));
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_VulkanRenderThread_setVulkanRotation(JNIEnv* env,
                                                        jobject /*thiz*/,
                                                        jstring player_id,
                                                        jint rotation) {
    auto ctx = VulkanContextManager::shareInstance().find(JNI::FromJVM::str(env, player_id));
    if (ctx) {
        ctx->setRotation(rotation);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_VulkanRenderThread_resizeVulkanSurface(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jstring player_id,
                                                          jint width,
                                                          jint height) {
    auto ctx = VulkanContextManager::shareInstance().find(JNI::FromJVM::str(env, player_id));
    if (ctx) {
        ctx->recreateSwapchain(nullptr, width, height);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_VulkanRenderThread_rebuildVulkanSurface(JNIEnv* env,
                                                           jobject /*thiz*/,
                                                           jstring player_id,
                                                           jobject surface) {
    auto playerId = JNI::FromJVM::str(env, player_id);
    auto ctx = VulkanContextManager::shareInstance().find(playerId);
    if (!ctx) {
        return;
    }
    auto* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        return;
    }
    ctx->recreateSwapchain(window, 0, 0);
    ANativeWindow_release(window);
}

} // namespace slark
