
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//


package com.slark.sdk

import android.view.Choreographer
import android.view.Surface

// OpenGL-backed render thread implementing IRenderThread
class GLRenderThread(
    private val surface: Surface,
    private var width: Int,
    private var height: Int
) : Thread(), IRenderThread {
    override var playerId: String = ""
    private val renderer: IPreviewRenderer = GLPreviewRenderer()
    @Volatile
    private var running = true
    private val lock = Object()
    @Volatile
    private var pendingTexture: RenderTexture = RenderTexture.default()
    @Volatile
    private var hasRenderRequest = false
    @Volatile
    private var isRebuildingSurface = false

    // Choreographer 驱动 VSync 节拍，通知 C++ 侧拉帧
    private val vsyncCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            if (!running) return
            if (playerId.isNotEmpty()) {
                onVSync(playerId)
            }
            // 重新注册，持续接收下一帧 VSync
            Choreographer.getInstance().postFrameCallback(this)
        }
    }

    override fun run() {
        if (playerId.isEmpty()) {
            SlarkLog.e(LOG_TAG, "playerId is empty")
            return
        }
        if (!createEGLContext(playerId, surface)) {
            SlarkLog.e(LOG_TAG, "createEGLContext failed")
            return
        }
        renderer.init()
        renderer.setSurfaceSize(width, height)
        // 在 GL 线程启动后，向主线程 Choreographer 注册 VSync 回调
        android.os.Handler(android.os.Looper.getMainLooper()).post {
            Choreographer.getInstance().postFrameCallback(vsyncCallback)
        }
        while (running) {
            synchronized(lock) {
                if (!hasRenderRequest) {
                    try { lock.wait(100) } catch (_: InterruptedException) { Thread.currentThread().interrupt() }
                }
                hasRenderRequest = false
            }
            if (!running) break
            if (isRebuildingSurface) {
                rebuildSurface(playerId, surface)
                isRebuildingSurface = false
            }
            if (renderer.drawFrame(pendingTexture)) {
                swapGLBuffers(playerId, if (pendingTexture.isBackup) 0 else pendingTexture.textureId)
            } else {
                SlarkLog.e(LOG_TAG, "render error:{}", pendingTexture.textureId)
            }
        }
        renderer.release()
        destroyEGLContext(playerId)
        surface.release()
        // 确保 VSync 回调已取消
        android.os.Handler(android.os.Looper.getMainLooper()).post {
            Choreographer.getInstance().removeFrameCallback(vsyncCallback)
        }
    }

    override fun render(textureId: Int, width: Int, height: Int) {
        synchronized(lock) {
            pendingTexture = RenderTexture(textureId, width, height)
            hasRenderRequest = true
            lock.notify()
        }
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
        // 立即取消 VSync 回调，避免空转
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
        renderer.setRotation(rotation.toDouble())
    }

    override fun setRenderSize(width: Int, height: Int) {
        if (width <= 0 || height <= 0) {
            SlarkLog.e(LOG_TAG, "Invalid render size: $width x $height")
            return
        }
        this.width = width
        this.height = height
        renderer.setSurfaceSize(width, height)
    }

    override fun rebuildSurface() {
        val backupTexture = getBackupTextureId(playerId)
        if (backupTexture != null && backupTexture.isValid()) {
            val backup = backupTexture.asBackup()
            isRebuildingSurface = true
            render(backup)
        } else {
            SlarkLog.w(LOG_TAG, "No valid backup texture available")
        }
    }

    private external fun createEGLContext(playerId: String, surface: Surface): Boolean
    private external fun destroyEGLContext(playerId: String)
    private external fun swapGLBuffers(playerId: String, textureId: Int): Boolean
    private external fun getBackupTextureId(playerId: String): RenderTexture?
    private external fun rebuildSurface(playerId: String, surface: Surface)
    /// Notify C++ VideoRender::onVSync() on each VSync beat.
    private external fun onVSync(playerId: String)

    companion object {
        const val LOG_TAG = "GLRenderThread"
    }
}
