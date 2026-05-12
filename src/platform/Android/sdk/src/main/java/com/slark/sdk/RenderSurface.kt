
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

class RenderSurface : SurfaceTexture.OnFrameAvailableListener, IGraphicsSurface {
    private var render: TextureRender? = null
    private var surfaceTexture: SurfaceTexture? = null
    private var _surface: Surface? = null
    private val mutex = Object()
    private var isFrameAvailable = false
    private var handlerThread: HandlerThread? = null
    private var handler: Handler? = null

    override val surface: Surface?
        get() = if (_surface?.isValid == true) _surface else null

    override val isValid: Boolean
        get() = _surface?.isValid == true && render?.isValid() == true

    fun init() {
        render = TextureRender()
        if (render?.isValid() == false) {
            SlarkLog.e(LOG_TAG, "TextureRender initialization failed")
            throw RuntimeException("Render initialization failed")
        }
        handlerThread = HandlerThread("SurfaceCallbackThread")
        handlerThread?.start()
        handler = Handler(handlerThread!!.looper)

        surfaceTexture = SurfaceTexture(render?.getTextureId() ?: 0).apply {
            setOnFrameAvailableListener(this@RenderSurface, handler)
        }
        _surface = Surface(surfaceTexture)
    }

    override fun onFrameAvailable(surfaceTexture: SurfaceTexture?) {
        synchronized(mutex) {
            isFrameAvailable = true
            mutex.notifyAll()
        }
    }

    override fun release() {
        _surface?.release()
        surfaceTexture?.release()
        surfaceTexture?.setOnFrameAvailableListener(null)
        render?.release()
        handlerThread?.quitSafely()
        _surface = null
        surfaceTexture = null
        render = null
        handlerThread = null
    }

    override fun drawFrame(format: MediaFormat, size: Size): Boolean {
        val tex = surfaceTexture
        val r = render
        if (tex == null || r == null) {
            SlarkLog.e(LOG_TAG, "drawFrame skipped: surfaceTexture or render is null")
            return false
        }
        return r.drawFrame(tex, format, size)
    }

    override fun awaitFrame(waitTime: Long): Pair<Boolean, Long> {
        synchronized(mutex) {
            if (!isFrameAvailable) {
                mutex.wait(waitTime)
                if (!isFrameAvailable) {
                    SlarkLog.e(LOG_TAG, "wait frame timeout")
                    return false to 0
                }
            }
            isFrameAvailable = false
        }
        if (!checkGLStatus("wait frame")) {
            SlarkLog.e(LOG_TAG, "checkGLStatus failed")
            return false to 0
        }
        var timestamp = 0L
        try {
            surfaceTexture?.let {
                it.updateTexImage()
                timestamp = it.timestamp / 1000L
            }
        } catch (e: Exception) {
            SlarkLog.e(LOG_TAG, "updateTexImage failed: ${e.message}")
            return false to 0
        }
        return true to timestamp
    }

    companion object {
        const val LOG_TAG = "RenderSurface"
    }
}
