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

// Common lifecycle skeleton for the GL/Vulkan render threads: vsync driving, start/stop,
// shutdown/join timeout fallback, and handler orchestration for rotation/size/surface rebuild.
// Backend-specific logic is implemented by subclasses.
abstract class RenderThreadBase(
    protected val surface: Surface,
    protected var width: Int,
    protected var height: Int,
    private val threadName: String
) : IRenderThread {

    override var playerId: String = ""

    protected abstract val logTag: String
    protected abstract val renderer: IPreviewRenderer

    private val handlerThread = HandlerThread("$threadName[$playerId]").apply { start() }
    private val handler = Handler(handlerThread.looper)

    @Volatile
    private var vsyncActive = false

    @Volatile
    protected var backendInitialized = false

    @Volatile
    private var shutdownRequested = false

    @Volatile
    private var startPending = false

    @Volatile
    protected var pendingTexture: RenderTexture = RenderTexture.default()

    private val vsyncCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            if (!vsyncActive) {
                return
            }
            if (playerId.isNotEmpty()) {
                try {
                    notifyVSync(playerId)
                } catch (e: Exception) {
                    SlarkLog.e(logTag, "doFrame: onVSync JNI exception: {0}", e.message ?: "")
                }
            }
            if (vsyncActive) {
                Choreographer.getInstance().postFrameCallback(this)
            }
        }
    }

    override fun initContext() {
        handler.post {
            if (backendInitialized) return@post
            if (shutdownRequested) {
                SlarkLog.w(logTag, "initContext: shutdown requested, skip")
                return@post
            }
            if (playerId.isEmpty()) {
                SlarkLog.w(logTag, "initContext: playerId is empty, thread may be discarded")
                return@post
            }
            if (initBackend()) {
                renderer.init()
                renderer.setSurfaceSize(width, height)
                backendInitialized = true
                if (startPending) {
                    start()
                }
            } else {
                SlarkLog.e(logTag, "initContext: init backend failed")
            }
        }
    }

    override fun start() {
        if (vsyncActive) return
        if (shutdownRequested) {
            SlarkLog.w(logTag, "start: shutdown requested, skip")
            return
        }
        if (playerId.isEmpty()) {
            SlarkLog.w(logTag, "start: playerId is empty, thread may be discarded")
            return
        }
        if (!backendInitialized) {
            startPending = true
            return
        }
        startPending = false
        vsyncActive = true
        handler.post {
            if (shutdownRequested) {
                SlarkLog.w(logTag, "start: handler post after shutdown, skip")
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
        render(RenderTexture(textureId, width, height))
    }

    override fun render(texture: RenderTexture) {
        if (!texture.isValid()) {
            SlarkLog.e(logTag, "render: invalid texture: $texture")
            return
        }
        handler.post {
            pendingTexture = texture
            presentTexture()
        }
    }

    override fun shutdown() {
        shutdownRequested = true
        stop()
        val latch = CountDownLatch(1)
        handler.post {
            if (backendInitialized) {
                renderer.release()
                destroyBackend()
                backendInitialized = false
            }
            latch.countDown()
        }
        // 3s timeout prevents a blocked backend fence from hanging shutdown
        if (!latch.await(3, TimeUnit.SECONDS)) {
            SlarkLog.w(logTag, "shutdown: destroy backend timed out, force quit")
        }
        handlerThread.quitSafely()
    }

    override fun join() {
        handlerThread.join(3000)
        if (handlerThread.isAlive) {
            SlarkLog.w(logTag, "join: handlerThread did not terminate within 3s, force interrupt")
            handlerThread.interrupt()
            handlerThread.join(1000)
        }
    }

    override fun setRotation(rotation: Int) {
        if (rotation < 0 || rotation > 360 || rotation % 90 != 0) {
            SlarkLog.e(logTag, "setRotation: invalid value: $rotation")
            return
        }
        SlarkLog.i(logTag, "setRotation: degrees=$rotation")
        handler.post {
            applyRotation(rotation)
            val backup = fetchBackupTextureId(playerId)
            if (backup != null && backup.isValid()) {
                pendingTexture = backup.asBackup()
                presentTexture()
            }
        }
    }

    override fun setRenderSize(width: Int, height: Int) {
        if (width <= 0 || height <= 0) {
            SlarkLog.e(logTag, "setRenderSize: invalid size: $width x $height")
            return
        }
        this.width = width
        this.height = height
        handler.post {
            renderer.setSurfaceSize(width, height)
            resizeBackend(width, height)
        }
    }

    override fun rebuildSurface() {
        val backupTexture = fetchBackupTextureId(playerId)
        if (backupTexture != null && backupTexture.isValid()) {
            val backup = backupTexture.asBackup()
            handler.post {
                rebuildBackend()
                pendingTexture = backup
                presentTexture()
            }
        } else {
            SlarkLog.w(logTag, "rebuildSurface: no valid backup texture")
        }
    }

    protected abstract fun initBackend(): Boolean

    protected abstract fun destroyBackend()

    protected abstract fun presentTexture()

    protected abstract fun applyRotation(rotation: Int)

    protected abstract fun rebuildBackend()

    protected open fun resizeBackend(width: Int, height: Int) {}

    protected abstract fun fetchBackupTextureId(playerId: String): RenderTexture?

    protected abstract fun notifyVSync(playerId: String)
}
