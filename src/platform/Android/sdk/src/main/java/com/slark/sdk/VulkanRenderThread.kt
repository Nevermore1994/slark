//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.sdk

import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.view.Choreographer
import android.view.Surface

// Vulkan-backed render thread, symmetric to iOS RenderMTKView.
// Uses HandlerThread + Handler (message-driven), matching iOS dispatch_async(renderQueue).
class VulkanRenderThread(
    private val surface: Surface,
    private var width: Int,
    private var height: Int
) : IRenderThread {

    override var playerId: String = ""
    private val renderer: IPreviewRenderer = VulkanPreviewRenderer()

    // HandlerThread for Vulkan rendering, symmetric to iOS renderQueue (serial GCD queue)
    private val handlerThread = HandlerThread("VulkanRenderThread[$playerId]").apply { start() }
    private val handler = Handler(handlerThread.looper)

    // VSync state, controlled by start()/stop()
    @Volatile
    private var vsyncActive = false

    // Track if Vulkan context is initialized
    @Volatile
    private var vulkanInitialized = false

    // startRendering 在 Vulkan 尚未就绪时被调用，等 initContext 完成后自动启动 VSync
    @Volatile
    private var startPending = false

    // Pending texture for render, protected by handler posting (single-threaded render queue)
    @Volatile
    private var pendingTexture: RenderTexture = RenderTexture.default()

    // Choreographer VSync callback, symmetric to iOS CADisplayLink
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
            // Re-register for next VSync
            if (vsyncActive) {
                Choreographer.getInstance().postFrameCallback(this)
            }
        }
    }

    // Init Vulkan context on render thread, called after playerId is set.
    // Symmetric to iOS setContext: -> setupMetal on renderQueue.
    override fun initContext() {
        handler.post {
            if (vulkanInitialized) return@post
            if (playerId.isEmpty()) {
                SlarkLog.e(LOG_TAG, "initContext: playerId is empty")
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

    // Register VSync (symmetric to iOS CADisplayLink start)
    override fun start() {
        if (vsyncActive) return
        if (playerId.isEmpty()) {
            SlarkLog.e(LOG_TAG, "start: playerId is empty")
            return
        }
        if (!vulkanInitialized) {
            startPending = true
            return
        }
        startPending = false
        vsyncActive = true
        // Choreographer.getInstance() 要求调用线程有 Looper
        if (Looper.myLooper() != null) {
            Choreographer.getInstance().postFrameCallback(vsyncCallback)
        } else {
            handler.post {
                Choreographer.getInstance().postFrameCallback(vsyncCallback)
            }
        }
    }

    override fun stop() {
        vsyncActive = false
        if (Looper.myLooper() != null) {
            Choreographer.getInstance().removeFrameCallback(vsyncCallback)
        } else {
            handler.post {
                Choreographer.getInstance().removeFrameCallback(vsyncCallback)
            }
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
        stop()
        handler.post {
            if (vulkanInitialized) {
                renderer.release()
                destroyVulkanContext(playerId)
                vulkanInitialized = false
            }
        }
        handlerThread.quitSafely()
    }

    override fun join() {
        handlerThread.join()
    }

    override fun setRotation(rotation: Int) {
        if (rotation < 0 || rotation > 360 || rotation % 90 != 0) {
            SlarkLog.e(LOG_TAG, "setRotation: invalid value: $rotation")
            return
        }
        handler.post {
            renderer.setRotation(rotation.toDouble())
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
                rebuildVulkanSurface(playerId, surface)
                pendingTexture = backup
                drawAndPresent()
            }
        } else {
            SlarkLog.w(LOG_TAG, "rebuildSurface: no valid backup texture")
        }
    }

    // Draw pendingTexture and present, must be called on render handler thread
    private fun drawAndPresent() {
        if (!vulkanInitialized) return
        if (renderer.drawFrame(pendingTexture)) {
            presentVulkanFrame(playerId, if (pendingTexture.isBackup) 0 else pendingTexture.textureId)
        } else {
            SlarkLog.e(LOG_TAG, "drawAndPresent: render error: {0}", pendingTexture.textureId)
        }
    }

    private external fun createVulkanContext(playerId: String, surface: Surface, width: Int, height: Int): Boolean
    private external fun destroyVulkanContext(playerId: String)
    private external fun presentVulkanFrame(playerId: String, textureId: Int)
    private external fun getBackupTextureId(playerId: String): RenderTexture?
    private external fun rebuildVulkanSurface(playerId: String, surface: Surface)
    // Notify C++ VideoRender::onVSync() on each VSync beat
    private external fun onVSync(playerId: String)

    companion object {
        const val LOG_TAG = "VulkanRenderThread"
    }
}
