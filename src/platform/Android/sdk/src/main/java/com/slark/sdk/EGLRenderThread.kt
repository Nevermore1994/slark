package com.slark.sdk

import android.view.Surface

class EGLRenderThread(
    private val surface: Surface,
    private var width: Int,
    private var height: Int
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
    @Volatile
    private var isRebuildingSurface = false

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

            if (isRebuildingSurface) {
                SlarkLog.i(LOG_TAG, "Rebuilding surface with texture: ${pendingTexture.textureId}")
                rebuildSurface(playerId, surface)
                isRebuildingSurface = false
            }

            if(renderer.drawFrame(pendingTexture)) {
                swapGLBuffers(playerId, if (pendingTexture.isBackup) 0 else pendingTexture.textureId)
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
            pendingTexture = RenderTexture(textureId, width, height)
            hasRenderRequest = true
            lock.notify()
        }
    }

    fun render(texture: RenderTexture) {
        if (!texture.isValid()) {
            SlarkLog.e(LOG_TAG, "Invalid texture for rendering: $texture")
            return
        }
        SlarkLog.i(LOG_TAG, "render textureId: ${texture.textureId}")
        synchronized(lock) {
            pendingTexture = texture
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

    fun setRotation(rotation: Int) {
        if (rotation < 0 || rotation > 360 || rotation % 90 != 0) {
            SlarkLog.e(LOG_TAG, "Invalid rotation value: $rotation")
            return
        }
        renderer.rotation = rotation.toDouble()
    }

    fun setRenderSize(width: Int, height: Int) {
        if (width <= 0 || height <= 0) {
            SlarkLog.e(LOG_TAG, "Invalid render size: $width x $height")
            return
        }
        this.width = width
        this.height = height
        renderer.onSurfaceChanged(null, width, height)
    }

    fun rebuildSurface() {
        val backupTexture = getBackupTextureId(playerId)
        if (backupTexture != null && backupTexture.isValid()) {
            backupTexture.isBackup = true
            SlarkLog.i(LOG_TAG, "Using backup texture: ${backupTexture.textureId}")
            isRebuildingSurface = true
            render(backupTexture)
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
        const val LOG_TAG = "EGLRenderThread"
    }
}