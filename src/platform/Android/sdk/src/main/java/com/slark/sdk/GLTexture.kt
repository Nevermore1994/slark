
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import android.opengl.GLES20

class GLTexture(
    override val width: Int,
    override val height: Int,
    private val config: TextureConfig = TextureConfig()
) : ITexture {
    override val id: Int
    private var released = false

    data class TextureConfig(
        val minFilter: Int = GLES20.GL_LINEAR,
        val magFilter: Int = GLES20.GL_LINEAR,
        val wrapS: Int = GLES20.GL_CLAMP_TO_EDGE,
        val wrapT: Int = GLES20.GL_CLAMP_TO_EDGE,
        val format: Int = GLES20.GL_RGBA,
        val internalFormat: Int = GLES20.GL_RGBA,
        val type: Int = GLES20.GL_UNSIGNED_BYTE
    )

    init {
        val ids = IntArray(1)
        GLES20.glGenTextures(1, ids, 0)
        id = ids[0]
        if (id == 0) {
            SlarkLog.e(LOG_TAG, "glGenTextures failed")
        } else {
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, id)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, config.minFilter)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, config.magFilter)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, config.wrapS)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, config.wrapT)
            // allocate storage
            GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, config.internalFormat, width, height, 0, config.format, config.type, null)
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
            checkGLStatus("GLTexture init")
        }
    }

    override fun bind(unit: Int) {
        if (released) return
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0 + unit)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, id)
    }

    override fun unbind() {
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
    }

    override fun release() {
        if (released) return
        released = true
        if (id != 0) {
            GLES20.glDeleteTextures(1, intArrayOf(id), 0)
        }
    }

    companion object {
        const val LOG_TAG = "GLTexture"
    }
}
