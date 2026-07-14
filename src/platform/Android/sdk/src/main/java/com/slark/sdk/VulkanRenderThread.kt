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

class VulkanRenderThread(
    private val surface: Surface,
    private var width: Int,
    private var height: Int
) : IRenderThread {

    override var playerId: String = ""
    private val renderer: IPreviewRenderer = VulkanPreviewRenderer()

    private val handlerThread = HandlerThread("VulkanRenderThread[$playerId]").apply { start() }
    private val handler = Handler(handlerThread.looper)

    @Volatile
    private var vsyncActive = false

    @Volatile
    private var vulkanInitialized = false

    @Volatile
    private var startPending = false

    @Volatile
    private var shutdownRequested = false

    @Volatile
    private var pendingTexture: RenderTexture = RenderTexture.default()

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

    override fun initContext() {
        handler.post {
            if (vulkanInitialized) return@post
            if (shutdownRequested) {
                SlarkLog.w(LOG_TAG, "initContext: shutdown requested, skip")
                return@post
            }
            if (playerId.isEmpty()) {
                SlarkLog.w(LOG_TAG, "initContext: playerId is empty, thread may be discarded")
                return@post
            }
            if (createVulkanContext(playerId, surface, width, height)) {
                renderer.init()
                renderer.setSurfaceSize(width, height)
                vulkanInitialized = true
                if (startPending) {
                    start()
                }
            } else {
                SlarkLog.e(LOG_TAG, "initContext: createVulkanContext failed")
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
        if (!vulkanInitialized) {
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
        render(RenderTexture(textureId, width, height))
    }

    override fun render(texture: RenderTexture) {
        if (!texture.isValid()) {
            SlarkLog.e(LOG_TAG, "render: invalid texture: $texture")
            return
        }
        handler.post {
            pendingTexture = texture
            drawAndPresent()
        }
    }

    override fun shutdown() {
        shutdownRequested = true
        stop()
        val latch = CountDownLatch(1)
        handler.post {
            if (vulkanInitialized) {
                renderer.release()
                destroyVulkanContext(playerId)
                vulkanInitialized = false
            }
            latch.countDown()
        }
        // 超时 3s 防止 Vulkan fence 阻塞导致 shutdown 卡死
        if (!latch.await(3, TimeUnit.SECONDS)) {
            SlarkLog.w(LOG_TAG, "shutdown: destroyVulkanContext timed out, force quit")
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
            setVulkanRotation(playerId, rotation)
            val backup = getBackupTextureId(playerId)
            if (backup != null && backup.isValid()) {
                pendingTexture = backup.asBackup()
                drawAndPresent()
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
            resizeVulkanSurface(playerId, width, height)
        }
    }

    override fun rebuildSurface() {
        val backupTexture = getBackupTextureId(playerId)
        if (backupTexture != null && backupTexture.isValid()) {
            val backup = backupTexture.asBackup()
            handler.post {
                rebuildVulkanSurface(playerId, surface)
                pendingTexture = backup
                drawAndPresent()
            }
        } else {
            SlarkLog.w(LOG_TAG, "rebuildSurface: no valid backup texture")
        }
    }

    private fun drawAndPresent() {
        if (!vulkanInitialized) return
        if (!pendingTexture.isValid()) {
            SlarkLog.w(LOG_TAG, "drawAndPresent: skip invalid texture: $pendingTexture")
            return
        }
        drawFrame(playerId, pendingTexture.textureId, pendingTexture.width, pendingTexture.height)
        presentVulkanFrame(playerId, if (pendingTexture.isBackup) 0 else pendingTexture.textureId)
    }

    private external fun createVulkanContext(playerId: String, surface: Surface, width: Int, height: Int): Boolean
    private external fun destroyVulkanContext(playerId: String)
    private external fun drawFrame(playerId: String, textureId: Int, width: Int, height: Int)
    private external fun presentVulkanFrame(playerId: String, textureId: Int)
    private external fun getBackupTextureId(playerId: String): RenderTexture?
    private external fun rebuildVulkanSurface(playerId: String, surface: Surface)
    private external fun resizeVulkanSurface(playerId: String, width: Int, height: Int)
    private external fun onVSync(playerId: String)
    private external fun setVulkanRotation(playerId: String, rotation: Int)

    companion object {
        const val LOG_TAG = "VulkanRenderThread"
    }
}
