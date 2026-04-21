
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import android.opengl.GLES20

class GLResourcePool {
    private val texturePool = mutableMapOf<String, MutableList<GLTexture>>()
    private val frameBufferPool = mutableMapOf<String, MutableList<GLFrameBuffer>>()
    private val lock = Any()

    fun acquireTexture(width: Int, height: Int, config: GLTexture.TextureConfig = GLTexture.TextureConfig()): GLTexture {
        synchronized(lock) {
            val key = textureKey(width, height, config)
            val pool = texturePool[key]
            if (!pool.isNullOrEmpty()) {
                return pool.removeAt(pool.lastIndex)
            }
        }
        return GLTexture(width, height, config)
    }

    fun releaseTexture(texture: GLTexture) {
        synchronized(lock) {
            val key = textureKey(texture.width, texture.height, GLTexture.TextureConfig())
            val pool = texturePool.getOrPut(key) { mutableListOf() }
            pool.add(texture)
        }
    }

    fun acquireFrameBuffer(width: Int, height: Int): GLFrameBuffer {
        synchronized(lock) {
            val key = fbKey(width, height)
            val pool = frameBufferPool[key]
            if (!pool.isNullOrEmpty()) {
                return pool.removeAt(pool.lastIndex)
            }
        }
        return GLFrameBuffer(width, height)
    }

    fun releaseFrameBuffer(fb: GLFrameBuffer) {
        synchronized(lock) {
            val key = fbKey(fb.width, fb.height)
            val pool = frameBufferPool.getOrPut(key) { mutableListOf() }
            pool.add(fb)
        }
    }

    fun clear() {
        synchronized(lock) {
            texturePool.values.flatten().forEach { it.release() }
            texturePool.clear()
            frameBufferPool.values.flatten().forEach { it.release() }
            frameBufferPool.clear()
        }
    }

    private fun textureKey(w: Int, h: Int, config: GLTexture.TextureConfig): String =
        "${w}x${h}_${config.format}_${config.internalFormat}_${config.type}"

    private fun fbKey(w: Int, h: Int): String = "${w}x${h}"

    companion object {
        const val LOG_TAG = "GLResourcePool"
    }
}
