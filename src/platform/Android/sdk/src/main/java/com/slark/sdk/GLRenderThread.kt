//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.sdk

import android.os.Handler
import android.os.HandlerThread
import android.view.Choreographer
import android.view.Surface
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class GLRenderThread(
    private val surface: Surface,
    private var width: Int,
    private var height: Int
) : IRenderThread {

    override var playerId: String = ""
    private val renderer: IPreviewRenderer = GLPreviewRenderer()

    private val handlerThread = HandlerThread("GLRenderThread[$playerId]").apply { start() }
    private val handler = Handler(handlerThread.looper)

    @Volatile
    private var vsyncActive = false

    private val vsyncCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            if (!vsyncActive) {
                return
            }
            if (playerId.isNotEmpty()) {
                try {
                    onVSync(playerId)
                } catch (e: Exception) {
                    SlarkLog.e(LOG_TAG, "doFrame: onVSync JNI exception: {0}", e.message ?: "")
                }
            }
            if (vsyncActive) {
                Choreographer.getInstance().postFrameCallback(this)
            }
        }
    }

    @Volatile
    private var pendingTexture: RenderTexture = RenderTexture.default()

    @Volatile
    private var eglInitialized = false

    @Volatile
    private var shutdownRequested = false

    @Volatile
    private var startPending = false

    override fun initContext() {
        handler.post {
            if (eglInitialized) return@post
            if (shutdownRequested) {
                SlarkLog.w(LOG_TAG, "initContext: shutdown requested, skip")
                return@post
            }
            if (playerId.isEmpty()) {
                SlarkLog.w(LOG_TAG, "initContext: playerId is empty, thread may be discarded")
                return@post
            }
            if (createEGLContext(playerId, surface)) {
                renderer.init()
                renderer.setSurfaceSize(width, height)
                eglInitialized = true
                if (startPending) {
                    start()
                }
            } else {
                SlarkLog.e(LOG_TAG, "initContext: createEGLContext failed")
            }
        }
    }

    override fun start() {
        if (vsyncActive) return
        if (shutdownRequested) {
            SlarkLog.w(LOG_TAG, "start: shutdown requested, skip")
            return
        }
        if (playerId.isEmpty()) {
            SlarkLog.w(LOG_TAG, "start: playerId is empty, thread may be discarded")
            return
        }
        if (!eglInitialized) {
            startPending = true
            return
        }
        startPending = false
        vsyncActive = true
        handler.post {
            if (shutdownRequested) {
                SlarkLog.w(LOG_TAG, "start: handler post after shutdown, skip")
                return@post
            }
            Choreographer.getInstance().postFrameCallback(vsyncCallback)
        }
    }

    override fun stop() {
        vsyncActive = false
        handler.post {
            Choreographer.getInstance().removeFrameCallback(vsyncCallback)
        }
    }

    override fun render(textureId: Int, width: Int, height: Int) {
        handler.post {
            pendingTexture = RenderTexture(textureId, width, height)
            drawAndSwap()
        }
    }

    override fun render(texture: RenderTexture) {
        if (!texture.isValid()) {
            SlarkLog.e(LOG_TAG, "render: invalid texture: $texture")
            return
        }
        handler.post {
            pendingTexture = texture
            drawAndSwap()
        }
    }

    override fun shutdown() {
        shutdownRequested = true
        stop()
        val latch = CountDownLatch(1)
        handler.post {
            if (eglInitialized) {
                renderer.release()
                destroyEGLContext(playerId)
                eglInitialized = false
            }
            latch.countDown()
        }
        if (!latch.await(3, TimeUnit.SECONDS)) {
            SlarkLog.w(LOG_TAG, "shutdown: destroyEGLContext timed out, force quit")
        }
        handlerThread.quitSafely()
    }

    override fun join() {
        handlerThread.join(3000)
        if (handlerThread.isAlive) {
            SlarkLog.w(LOG_TAG, "join: handlerThread did not terminate within 3s, force interrupt")
            handlerThread.interrupt()
            handlerThread.join(1000)
        }
    }

    override fun setRotation(rotation: Int) {
        if (rotation < 0 || rotation > 360 || rotation % 90 != 0) {
            SlarkLog.e(LOG_TAG, "setRotation: invalid value: $rotation")
            return
        }
        SlarkLog.i(LOG_TAG, "setRotation: degrees=$rotation")
        handler.post {
            renderer.setRotation(rotation.toDouble())
            val backup = getBackupTextureId(playerId)
            if (backup != null && backup.isValid()) {
                pendingTexture = backup.asBackup()
                drawAndSwap()
            }
        }
    }

    override fun setRenderSize(width: Int, height: Int) {
        if (width <= 0 || height <= 0) {
            SlarkLog.e(LOG_TAG, "setRenderSize: invalid size: $width x $height")
            return
        }
        this.width = width
        this.height = height
        handler.post {
            renderer.setSurfaceSize(width, height)
        }
    }

    override fun rebuildSurface() {
        val backupTexture = getBackupTextureId(playerId)
        if (backupTexture != null && backupTexture.isValid()) {
            val backup = backupTexture.asBackup()
            handler.post {
                rebuildSurface(playerId, surface)
                pendingTexture = backup
                drawAndSwap()
            }
        } else {
            SlarkLog.w(LOG_TAG, "rebuildSurface: no valid backup texture")
        }
    }

    private fun drawAndSwap() {
        if (!eglInitialized) return
        if (renderer.drawFrame(pendingTexture)) {
            swapGLBuffers(playerId, if (pendingTexture.isBackup) 0 else pendingTexture.textureId)
        } else {
            SlarkLog.e(LOG_TAG, "drawAndSwap: render error: {0}", pendingTexture.textureId)
        }
    }

    private external fun createEGLContext(playerId: String, surface: Surface): Boolean
    private external fun destroyEGLContext(playerId: String)
    private external fun swapGLBuffers(playerId: String, textureId: Int): Boolean
    private external fun getBackupTextureId(playerId: String): RenderTexture?
    private external fun rebuildSurface(playerId: String, surface: Surface)
    private external fun onVSync(playerId: String)

    companion object {
        const val LOG_TAG = "GLRenderThread"
    }
}
