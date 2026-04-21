
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import android.graphics.SurfaceTexture
import android.media.MediaFormat
import android.os.Handler
import android.os.HandlerThread
import android.util.Size
import android.view.Surface

class GLGraphicsSurface : IGraphicsSurface {
    private var renderer: ITextureRenderer? = null
    private var surfaceTexture: SurfaceTexture? = null
    private var _surface: Surface? = null
    private val mutex = Object()
    private var isFrameAvailable = false
    private var handlerThread: HandlerThread? = null
    private var handler: Handler? = null

    override val surface: Surface?
        get() = if (_surface?.isValid == true) _surface else null

    override val isValid: Boolean
        get() = _surface?.isValid == true && renderer?.isValid == true

    fun init() {
        renderer = GLTextureRenderer()
        handlerThread = HandlerThread("GLSurfaceCallbackThread")
        handlerThread?.start()
        handler = Handler(handlerThread!!.looper)
        surfaceTexture = SurfaceTexture(renderer?.textureId ?: 0).apply {
            setOnFrameAvailableListener({ onFrameAvailable() }, handler)
        }
        _surface = Surface(surfaceTexture)
    }

    private fun onFrameAvailable() {
        synchronized(mutex) { isFrameAvailable = true; mutex.notifyAll() }
    }

    override fun awaitFrame(waitTime: Long): Pair<Boolean, Long> {
        synchronized(mutex) {
            if (!isFrameAvailable) {
                try { mutex.wait(waitTime) } catch (_: InterruptedException) { Thread.currentThread().interrupt() }
                if (!isFrameAvailable) return false to 0L
            }
            isFrameAvailable = false
        }
        if (!checkGLStatus("awaitFrame")) return false to 0L
        return try {
            surfaceTexture?.let {
                it.updateTexImage()
                true to (it.timestamp / 1000L)
            } ?: (false to 0L)
        } catch (e: Exception) {
            SlarkLog.e(LOG_TAG, "updateTexImage failed: ${e.message}")
            false to 0L
        }
    }

    override fun drawFrame(format: MediaFormat, size: Size): Boolean {
        val tex = surfaceTexture ?: return false
        val r = renderer ?: return false
        return r.drawFrame(tex, format, size)
    }

    override fun release() {
        _surface?.release()
        surfaceTexture?.setOnFrameAvailableListener(null)
        surfaceTexture?.release()
        renderer?.release()
        handlerThread?.quitSafely()
        _surface = null; surfaceTexture = null; renderer = null; handlerThread = null
    }

    companion object {
        const val LOG_TAG = "GLGraphicsSurface"
    }
}
