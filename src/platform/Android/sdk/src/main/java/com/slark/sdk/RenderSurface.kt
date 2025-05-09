package com.slark.sdk

import android.graphics.SurfaceTexture
import android.media.MediaFormat
import android.util.Size
import android.view.Surface

class RenderSurface : SurfaceTexture.OnFrameAvailableListener {
    private var render: TextureRender? = null
    private var surfaceTexture: SurfaceTexture? = null
    private var surface: Surface? = null
    private val mutex = Object()
    private var isFrameAvailable = false

    fun init() {
        render = TextureRender()
        surfaceTexture = SurfaceTexture(render?.getTextureId() ?: 0)
        surfaceTexture!!.setOnFrameAvailableListener(this)
        surface = Surface(surfaceTexture)
    }

    override fun onFrameAvailable(surfaceTexture: SurfaceTexture?) {
        synchronized(mutex) {
            isFrameAvailable = true
            mutex.notifyAll()
        }
    }

    fun surface(): Surface? {
        return surface
    }

    fun release() {
        surface?.release()
        surfaceTexture?.release()
        surfaceTexture?.setOnFrameAvailableListener(null)
        render?.release()
        surface = null
        surfaceTexture = null
        render = null
    }

    fun drawFrame(format: MediaFormat, size: Size) {
        surfaceTexture?.let { render?.drawFrame(it, format, size) }
    }

    fun awaitFrame(waitTime: Long): Pair<Boolean, Long> {
        synchronized(mutex) {
            mutex.wait(waitTime) // wait some time for frame available
            if (!isFrameAvailable) {
                SlarkLog.e(LOG_TAG, "wait frame timeout")
                return false to 0
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
                timestamp = it.timestamp / 1000000L //to ms
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
