
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import android.view.Surface

/ OpenGL-backed render thread implementing IRenderThread
class GLRenderThread(
    private val surface: Surface,
    private var width: Int,
    private var height: Int
) : Thread(), IRenderThread {
    override var playerId: String = ""
    private val renderer: IPreviewRenderer = GLPreviewRenderer().apply { init() }
    @Volatile
    private var running = true
    private val lock = Object()
    @Volatile
    private var pendingTexture: RenderTexture = RenderTexture.default()
    @Volatile
    private var hasRenderRequest = false
    @Volatile
    private var isRebuildingSurface = false

    override fun run() {
        if (playerId.isEmpty()) {
            SlarkLog.e(LOG_TAG, "playerId is empty")
            return
        }
        if (!createEGLContext(playerId, surface)) {
            SlarkLog.e(LOG_TAG, "createEGLContext failed")
            return
        }
        renderer.setSurfaceSize(width, height)
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

    companion object {
        const val LOG_TAG = "GLRenderThread"
    }
}
