//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.sdk

import android.view.Surface

class GLRenderThread(
    surface: Surface,
    width: Int,
    height: Int
) : RenderThreadBase(surface, width, height, "GLRenderThread") {

    override val logTag: String = LOG_TAG
    override val renderer: IPreviewRenderer = GLPreviewRenderer()

    override fun initBackend(): Boolean = createEGLContext(playerId, surface)

    override fun destroyBackend() {
        destroyEGLContext(playerId)
    }

    override fun presentTexture() {
        if (!backendInitialized) return
        if (renderer.drawFrame(pendingTexture)) {
            swapGLBuffers(playerId, if (pendingTexture.isBackup) 0 else pendingTexture.textureId)
        } else {
            SlarkLog.e(logTag, "drawAndSwap: render error: {0}", pendingTexture.textureId)
        }
    }

    override fun applyRotation(rotation: Int) {
        renderer.setRotation(rotation.toDouble())
    }

    override fun rebuildBackend() {
        rebuildSurface(playerId, surface)
    }

    override fun fetchBackupTextureId(playerId: String): RenderTexture? = getBackupTextureId(playerId)

    override fun notifyVSync(playerId: String) {
        onVSync(playerId)
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
