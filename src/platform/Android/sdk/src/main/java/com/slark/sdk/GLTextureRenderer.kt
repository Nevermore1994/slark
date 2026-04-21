
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import android.graphics.SurfaceTexture
import android.media.MediaFormat
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.opengl.Matrix
import android.util.Size
import java.nio.ByteBuffer
import java.nio.ByteOrder

class GLTextureRenderer : ITextureRenderer {
    private var program: Int = 0
    private val vbo = IntArray(1)
    override var textureId: Int = 0
        private set
    private var positionLoc: Int = 0
    private var texCoordLoc: Int = 0
    private var texMatrixLoc: Int = 0
    private var textureLoc: Int = 0
    private var mvpMatrixLoc: Int = 0

    override val isValid: Boolean
        get() = program != 0

    init {
        do {
            program = createGLProgram(VERTEX_SHADER, FRAGMENT_SHADER)
            if (program == 0) { SlarkLog.e(LOG_TAG, "create program failed"); break }

            GLES20.glGenBuffers(1, vbo, 0)
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vbo[0])
            val buf = ByteBuffer.allocateDirect(kVertices.size * 4)
                .order(ByteOrder.nativeOrder()).asFloatBuffer().put(kVertices)
            buf.position(0)
            GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, kVertices.size * 4, buf, GLES20.GL_STATIC_DRAW)
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)

            val ids = IntArray(1)
            GLES20.glGenTextures(1, ids, 0)
            textureId = ids[0]
            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId)
            if (!checkGLStatus("bind texture")) break
            GLES20.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR.toFloat())
            GLES20.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR.toFloat())
            GLES20.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE.toFloat())
            GLES20.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE.toFloat())
            if (!checkGLStatus("set texture param")) break

            texMatrixLoc = GLES20.glGetUniformLocation(program, "uTexMatrix")
            mvpMatrixLoc = GLES20.glGetUniformLocation(program, "uMVPMatrix")
            textureLoc = GLES20.glGetUniformLocation(program, "uTexture")
            positionLoc = GLES20.glGetAttribLocation(program, "a_position")
            texCoordLoc = GLES20.glGetAttribLocation(program, "a_texCoord")
        } while (false)
    }

    override fun drawFrame(surfaceTexture: SurfaceTexture, format: MediaFormat, size: Size): Boolean {
        if (!isValid) return false
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
        GLES20.glUseProgram(program)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vbo[0])
        GLES20.glEnableVertexAttribArray(positionLoc)
        GLES20.glVertexAttribPointer(0, 2, GLES20.GL_FLOAT, false, 16, 0)
        GLES20.glEnableVertexAttribArray(texCoordLoc)
        GLES20.glVertexAttribPointer(1, 2, GLES20.GL_FLOAT, false, 16, 8)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)

        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId)
        GLES20.glUniform1i(textureLoc, 0)

        val displayAspect = size.width.toFloat() / size.height.toFloat()
        val videoAspect = format.getInteger(MediaFormat.KEY_WIDTH).toFloat() /
            format.getInteger(MediaFormat.KEY_HEIGHT).toFloat()
        val mvp = FloatArray(16)
        Matrix.setIdentityM(mvp, 0)
        if (displayAspect > videoAspect) Matrix.scaleM(mvp, 0, videoAspect / displayAspect, -1f, 1f)
        else Matrix.scaleM(mvp, 0, 1f, displayAspect / videoAspect * -1.0f, 1f)
        GLES20.glUniformMatrix4fv(mvpMatrixLoc, 1, false, mvp, 0)

        val texMatrix = FloatArray(16)
        surfaceTexture.getTransformMatrix(texMatrix)
        GLES20.glUniformMatrix4fv(texMatrixLoc, 1, false, texMatrix, 0)

        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glUseProgram(0)
        GLES20.glFlush()
        return checkGLStatus("drawFrame")
    }

    override fun awaitFrame(waitTime: Long): Pair<Boolean, Long> {
        frame waiting handled by GLGraphicsSurface via SurfaceTexture
        return true to 0L
    }

    override fun release() {
        if (program != 0) { GLES20.glDeleteProgram(program); program = 0 }
        if (vbo[0] != 0) { GLES20.glDeleteBuffers(1, vbo, 0); vbo[0] = 0 }
        if (textureId != 0) { GLES20.glDeleteTextures(1, intArrayOf(textureId), 0); textureId = 0 }
    }

    companion object {
        const val LOG_TAG = "GLTextureRenderer"
        private val VERTEX_SHADER = """
            #version 100
            precision mediump float;
            attribute vec4 a_position;
            attribute vec2 a_texCoord;
            uniform mat4 uMVPMatrix;
            uniform mat4 uTexMatrix;
            varying vec2 v_texCoord;
            void main() {
                gl_Position = uMVPMatrix * a_position;
                vec4 texCoord = uTexMatrix * vec4(a_texCoord, 0.0, 1.0);
                v_texCoord = texCoord.xy;
            }
        """.trimIndent()
        private val FRAGMENT_SHADER = """
            #version 100
            #extension GL_OES_EGL_image_external : require
            precision mediump float;
            varying vec2 v_texCoord;
            uniform samplerExternalOES uTexture;
            void main() { gl_FragColor = texture2D(uTexture, v_texCoord); }
        """.trimIndent()
        private val kVertices = floatArrayOf(
            -1.0f, -1.0f, 0.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
            -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f
        )
    }
}
