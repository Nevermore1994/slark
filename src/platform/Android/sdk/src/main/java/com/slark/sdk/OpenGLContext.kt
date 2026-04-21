
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import android.view.Surface

class OpenGLContext(
    private val config: GraphicsConfig = GraphicsConfig(),
    private val playerId: String = ""
) : IGraphicsContext {
    override val backend = GraphicsBackend.OpenGL
    private var eglInitialized = false
    private var errorCallback: ((String) -> Unit)? = null
    private val resourcePool = GLResourcePool()

    override val isValid: Boolean
        get() = eglInitialized && checkEGLContext()

    override fun createSharedContext(): IGraphicsContext {
        val ctx = OpenGLContext(config, playerId)
        if (eglInitialized) {
            ctx.eglInitialized = true
        }
        return ctx
    }

    override fun createTexture(width: Int, height: Int): ITexture =
        resourcePool.acquireTexture(width, height)

    override fun createFrameBuffer(width: Int, height: Int): IFrameBuffer =
        resourcePool.acquireFrameBuffer(width, height)

    override fun createTextureRenderer(): ITextureRenderer = GLTextureRenderer()

    override fun createPreviewRenderer(): IPreviewRenderer = GLPreviewRenderer()

    override fun createSurface(): IGraphicsSurface {
        val surface = GLGraphicsSurface()
        surface.init()
        return surface
    }

    override fun swapBuffers() {
        buffer swap handled by GLRenderThread or C++ side
    }

    override fun makeCurrent() {
        EGL context attachment handled by C++ side via AndroidEGLContext
    }

    override fun release() {
        resourcePool.clear()
        eglInitialized = false
    }

    override fun onError(callback: (String) -> Unit) {
        errorCallback = callback
    }

    fun reportError(msg: String) {
        errorCallback?.invoke(msg)
        SlarkLog.e(LOG_TAG, msg)
    }

    companion object {
        const val LOG_TAG = "OpenGLContext"
    }
}
