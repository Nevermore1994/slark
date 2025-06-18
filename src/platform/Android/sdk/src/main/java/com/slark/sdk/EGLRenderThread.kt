package com.slark.sdk

import android.view.Surface
import android.util.Size

class EGLRenderThread(
    private val surface: Surface,
    private val width: Int,
    private val height: Int
) : Thread() {
    var playerId: String = ""
    private val renderer = PreviewRender()
    @Volatile
    private var running = true

    private val lock = Object()
    @Volatile
    private var pendingTexture: RenderTexture = RenderTexture.default()
    @Volatile
    private var hasRenderRequest = false

    override fun run() {
        super.run()
        if (playerId.isEmpty()) {
            SlarkLog.e(LOG_TAG, "playerId is empty")
            return
        }
        val isSuccess = createEGLContext(playerId, surface)
        if (!isSuccess) {
            SlarkLog.e(LOG_TAG, "createEGLContext failed")
            return
        }
        renderer.onSurfaceCreated(null, null)
        renderer.onSurfaceChanged(null, width, height)

        while (running) {
            synchronized(lock) {
                if (!hasRenderRequest) {
                    lock.wait()
                }
                hasRenderRequest = false
            }
            if (!running) break

            if(renderer.drawFrame(pendingTexture)) {
                swapGLBuffers(playerId, pendingTexture.textureId)
            } else {
                SlarkLog.e(LOG_TAG, "render error:{}", pendingTexture.textureId)
            }
        }

        renderer.release()
        destroyEGLContext(playerId)
        surface.release()
    }

    fun render(textureId: Int, width: Int, height: Int) {
        SlarkLog.i(LOG_TAG, "render textureId: $textureId")
        synchronized(lock) {
            pendingTexture = RenderTexture(textureId, Size(width, height))
            hasRenderRequest = true
            lock.notify()
        }
    }

    fun shutdown() {
        running = false
        synchronized(lock) {
            lock.notify()
        }
    }

    private external fun createEGLContext(playerId: String, surface: Surface): Boolean

    private external fun destroyEGLContext(playerId: String)

    private external fun swapGLBuffers(playerId: String, textureId: Int): Boolean

    companion object {
        const val LOG_TAG = "EGLRenderThread"
    }
}