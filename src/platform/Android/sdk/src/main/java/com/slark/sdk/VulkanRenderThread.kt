//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.sdk

import android.view.Surface

class VulkanRenderThread(
    surface: Surface,
    width: Int,
    height: Int
) : RenderThreadBase(surface, width, height, "VulkanRenderThread") {

    override val logTag: String = LOG_TAG
    override val renderer: IPreviewRenderer = VulkanPreviewRenderer()

    override fun initBackend(): Boolean = createVulkanContext(playerId, surface, width, height)

    override fun destroyBackend() {
        destroyVulkanContext(playerId)
    }

    override fun presentTexture() {
        if (!backendInitialized) return
        if (!pendingTexture.isValid()) {
            SlarkLog.w(logTag, "drawAndPresent: skip invalid texture: $pendingTexture")
            return
        }
        drawFrame(playerId, pendingTexture.textureId, pendingTexture.width, pendingTexture.height)
        presentVulkanFrame(playerId, if (pendingTexture.isBackup) 0 else pendingTexture.textureId)
    }

    override fun applyRotation(rotation: Int) {
        setVulkanRotation(playerId, rotation)
    }

    override fun rebuildBackend() {
        rebuildVulkanSurface(playerId, surface)
    }

    override fun resizeBackend(width: Int, height: Int) {
        resizeVulkanSurface(playerId, width, height)
    }

    override fun fetchBackupTextureId(playerId: String): RenderTexture? = getBackupTextureId(playerId)

    override fun notifyVSync(playerId: String) {
        onVSync(playerId)
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
