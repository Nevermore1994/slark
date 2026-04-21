
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import android.opengl.GLES20
import android.util.Size
import java.nio.ByteBuffer
import java.nio.ByteOrder

class GLPreviewRenderer : IPreviewRenderer {
    private var program: Int = 0
    private var positionHandle: Int = 0
    private var textureHandle: Int = 0
    private var texCoordHandle: Int = 0
    private var rotationHandle: Int = 0
    private var vertexVBO: Int = 0
    private var textureVBO: Int = 0
    private var surfaceSize: Size = Size(0, 0)
    @Volatile
    override var rotation: Double = 0.0

    fun init() {
        do {
            program = createGLProgram(VERTEX_SHADER, FRAGMENT_SHADER)
            if (program == 0) { SlarkLog.e(LOG_TAG, "create program failed"); break }
            positionHandle = GLES20.glGetAttribLocation(program, "aPosition")
            textureHandle = GLES20.glGetUniformLocation(program, "uTexture")
            texCoordHandle = GLES20.glGetAttribLocation(program, "aTexCoord")
            rotationHandle = GLES20.glGetUniformLocation(program, "uRotation")

            val buffers = IntArray(2)
            GLES20.glGenBuffers(2, buffers, 0)
            vertexVBO = buffers[0]
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vertexVBO)
            GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, 8 * Float.SIZE_BYTES, null, GLES20.GL_DYNAMIC_DRAW)
            GLES20.glVertexAttribPointer(positionHandle, 2, GLES20.GL_FLOAT, false, 0, 0)
            GLES20.glEnableVertexAttribArray(positionHandle)
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)

            val texBuf = ByteBuffer.allocateDirect(kTexCoords.size * Float.SIZE_BYTES)
                .order(ByteOrder.nativeOrder()).asFloatBuffer().apply { put(kTexCoords); position(0) }
            textureVBO = buffers[1]
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, textureVBO)
            GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, kTexCoords.size * Float.SIZE_BYTES, texBuf, GLES20.GL_STATIC_DRAW)
            GLES20.glVertexAttribPointer(texCoordHandle, 2, GLES20.GL_FLOAT, false, 0, 0)
            GLES20.glEnableVertexAttribArray(texCoordHandle)
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)
        } while (false)
    }

    override fun drawFrame(texture: RenderTexture): Boolean {
        if (program == 0 || !texture.isValid()) return false
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT or GLES20.GL_DEPTH_BUFFER_BIT)
        GLES20.glUseProgram(program)
        GLES20.glViewport(0, 0, surfaceSize.width, surfaceSize.height)

        val picSize = Size(texture.width, texture.height)
        val contentSize = makeAspectRatioSize(picSize, surfaceSize)
        val scale = Pair(
            contentSize.width.toFloat() / surfaceSize.width,
            contentSize.height.toFloat() / surfaceSize.height
        )
        val norm = if (scale.first > scale.second) Pair(1.0f, scale.second / scale.first)
                    else Pair(scale.first / scale.second, 1.0f)

        val verts = floatArrayOf(
            -norm.first, -norm.second, norm.first, -norm.second,
            -norm.first, norm.second, norm.first, norm.second
        )
        val vertBuf = ByteBuffer.allocateDirect(verts.size * Float.SIZE_BYTES)
            .order(ByteOrder.nativeOrder()).asFloatBuffer().apply { put(verts); position(0) }

        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture.textureId)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glUniform1i(textureHandle, 0)
        GLES20.glUniform1f(rotationHandle, Math.toRadians(rotation).toFloat())

        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vertexVBO)
        GLES20.glBufferSubData(GLES20.GL_ARRAY_BUFFER, 0, verts.size * Float.SIZE_BYTES, vertBuf)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)

        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)
        GLES20.glUseProgram(0)
        return true
    }

    override fun setSurfaceSize(width: Int, height: Int) {
        surfaceSize = Size(width, height)
        GLES20.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
    }

    override fun release() {
        if (program != 0) { GLES20.glDeleteProgram(program); program = 0 }
        if (vertexVBO != 0) {
            GLES20.glDeleteBuffers(2, intArrayOf(vertexVBO, textureVBO), 0)
            vertexVBO = 0; textureVBO = 0
        }
    }

    companion object {
        const val LOG_TAG = "GLPreviewRenderer"
        private val VERTEX_SHADER = """
            attribute vec4 aPosition;
            attribute vec2 aTexCoord;
            uniform float uRotation;
            varying vec2 vTexCoord;
            void main() {
                mat4 rot = mat4(cos(uRotation), -sin(uRotation), 0.0, 0.0,
                                sin(uRotation), cos(uRotation), 0.0, 0.0,
                                0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0);
                gl_Position = rot * aPosition;
                vTexCoord = aTexCoord;
            }
        """
        private val FRAGMENT_SHADER = """
            precision mediump float;
            varying vec2 vTexCoord;
            uniform sampler2D uTexture;
            void main() { gl_FragColor = texture2D(uTexture, vTexCoord); }
        """
        private val kTexCoords = floatArrayOf(0f, 0f, 1f, 0f, 0f, 1f, 1f, 1f)
    }
}
