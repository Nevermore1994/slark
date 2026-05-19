//
// Created by Nevermore- on 2025/5/6.
//

package com.slark.sdk

import android.view.Choreographer
import android.view.Surface

class VulkanRenderThread(
    private val surface: Surface,
    private var width: Int,
    private var height: Int
) : Thread(), IRenderThread {
    override var playerId: String = ""
    @Volatile
    private var running = true
    private val lock = Object()
    @Volatile
    private var pendingTexture: RenderTexture = RenderTexture.default()
    @Volatile
    private var hasRenderRequest = false
    @Volatile
    private var isRebuildingSurface = false
    @Volatile
    private var pendingSurface: Surface? = null

    private val vsyncCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            if (!running) return
            if (playerId.isNotEmpty()) {
                onVSync(playerId)
            }
            Choreographer.getInstance().postFrameCallback(this)
        }
    }

    override fun run() {
        if (playerId.isEmpty()) {
            SlarkLog.e(LOG_TAG, "playerId is empty")
            surface.release()
            return
        }
        if (!createVulkanContext(playerId, surface, width, height)) {
            SlarkLog.e(LOG_TAG, "createVulkanContext failed")
            surface.release()
            return
        }
        android.os.Handler(android.os.Looper.getMainLooper()).post {
            Choreographer.getInstance().postFrameCallback(vsyncCallback)
        }
        while (running) {
            synchronized(lock) {
                while (!hasRenderRequest && running) {
                    try { lock.wait(100) } catch (_: InterruptedException) { Thread.currentThread().interrupt() }
                }
                hasRenderRequest = false
            }
            if (!running) break
            if (isRebuildingSurface) {
                pendingSurface?.let { s -> rebuildVulkanSurface(playerId, s) }
                isRebuildingSurface = false
                pendingSurface = null
            }
            val tex = pendingTexture
            if (tex.isValid()) {
                drawFrame(playerId, tex.textureId, tex.width, tex.height)
                if (!tex.isBackup) {
                    renderFrameComplete(playerId, tex.textureId)
                }
            }
        }
        destroyVulkanContext(playerId)
        surface.release()
        android.os.Handler(android.os.Looper.getMainLooper()).post {
            Choreographer.getInstance().removeFrameCallback(vsyncCallback)
        }
    }

    override fun render(textureId: Int, width: Int, height: Int) {
        render(RenderTexture(textureId, width, height))
    }

    override fun render(texture: RenderTexture) {
        if (!texture.isValid()) {
            SlarkLog.e(LOG_TAG, "Invalid texture for rendering: $texture")
            return
        }
        synchronized(lock) {
            pendingTexture = texture
            hasRenderRequest = true
            lock.notify()
        }
    }

    override fun shutdown() {
        running = false
        android.os.Handler(android.os.Looper.getMainLooper()).post {
            Choreographer.getInstance().removeFrameCallback(vsyncCallback)
        }
        synchronized(lock) { lock.notify() }
    }

    override fun setRotation(rotation: Int) {
        if (rotation < 0 || rotation > 360 || rotation % 90 != 0) {
            SlarkLog.e(LOG_TAG, "Invalid rotation value: $rotation")
            return
        }
        setVulkanRotation(playerId, rotation)
    }

    override fun setRenderSize(width: Int, height: Int) {
        if (width <= 0 || height <= 0) {
            SlarkLog.e(LOG_TAG, "Invalid render size: $width x $height")
            return
        }
        this.width = width
        this.height = height
        resizeVulkanSurface(playerId, width, height)
    }

    override fun rebuildSurface() {
        val backupTexture = getBackupTextureId(playerId)
        synchronized(lock) {
            pendingSurface = surface
            isRebuildingSurface = true
            if (backupTexture != null && backupTexture.isValid()) {
                pendingTexture = backupTexture.asBackup()
            }
            hasRenderRequest = true
            lock.notify()
        }
    }

    private external fun createVulkanContext(playerId: String, surface: Surface, width: Int, height: Int): Boolean
    private external fun destroyVulkanContext(playerId: String)
    private external fun drawFrame(playerId: String, textureId: Int, width: Int, height: Int)
    private external fun renderFrameComplete(playerId: String, textureId: Int)
    private external fun getBackupTextureId(playerId: String): RenderTexture?
    private external fun setVulkanRotation(playerId: String, rotation: Int)
    private external fun resizeVulkanSurface(playerId: String, width: Int, height: Int)
    private external fun rebuildVulkanSurface(playerId: String, surface: Surface)
    private external fun onVSync(playerId: String)

    companion object {
        const val LOG_TAG = "VulkanRenderThread"
    }
}
