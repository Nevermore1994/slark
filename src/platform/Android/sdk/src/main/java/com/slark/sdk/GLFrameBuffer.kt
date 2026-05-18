
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//


package com.slark.sdk

import android.opengl.GLES20

class GLFrameBuffer(
    override val width: Int,
    override val height: Int
) : IFrameBuffer {
    override val id: Int
    private var colorTexture: ITexture? = null
    private var released = false

    init {
        val ids = IntArray(1)
        GLES20.glGenFramebuffers(1, ids, 0)
        id = ids[0]
        if (id == 0) {
            SlarkLog.e(LOG_TAG, "glGenFramebuffers failed")
        }
        checkGLStatus("GLFrameBuffer init")
    }

    override fun attachColorTexture(texture: ITexture) {
        if (released || id == 0) return
        colorTexture = texture
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, id)
        GLES20.glFramebufferTexture2D(GLES20.GL_FRAMEBUFFER, GLES20.GL_COLOR_ATTACHMENT0, GLES20.GL_TEXTURE_2D, texture.id, 0)
        val status = GLES20.glCheckFramebufferStatus(GLES20.GL_FRAMEBUFFER)
        if (status != GLES20.GL_FRAMEBUFFER_COMPLETE) {
            SlarkLog.e(LOG_TAG, "framebuffer incomplete: $status")
        }
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0)
    }

    override fun bind() {
        if (released) return
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, id)
        GLES20.glViewport(0, 0, width, height)
    }

    override fun unbind() {
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0)
    }

    override fun release() {
        if (released) return
        released = true
        colorTexture?.release()
        colorTexture = null
        if (id != 0) {
            GLES20.glDeleteFramebuffers(1, intArrayOf(id), 0)
        }
    }

    companion object {
        const val LOG_TAG = "GLFrameBuffer"
    }
}
