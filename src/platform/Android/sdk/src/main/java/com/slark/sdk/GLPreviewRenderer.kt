//
// Copyright (c) 2026 Nevermore-. All rights reserved.
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
    private var vertexVBO: Int = 0
    private var textureVBO: Int = 0
    private var surfaceSize: Size = Size(0, 0)
    @Volatile
    private var rotationDeg: Double = 0.0
    private val vertexBuffer: java.nio.FloatBuffer = ByteBuffer
        .allocateDirect(8 * Float.SIZE_BYTES)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()
    private val texCoordBuffer: java.nio.FloatBuffer = ByteBuffer
        .allocateDirect(8 * Float.SIZE_BYTES)
        .order(ByteOrder.nativeOrder())
        .asFloatBuffer()

    private var lastBoundTextureId: Int = 0

    override fun init() {
        program = createGLProgram(VERTEX_SHADER, FRAGMENT_SHADER)
        if (program == 0) {
            SlarkLog.e(LOG_TAG, "create program failed")
            return
        }
        positionHandle = GLES20.glGetAttribLocation(program, "aPosition")
        textureHandle = GLES20.glGetUniformLocation(program, "uTexture")
        texCoordHandle = GLES20.glGetAttribLocation(program, "aTexCoord")

        val buffers = IntArray(2)
        GLES20.glGenBuffers(2, buffers, 0)
        vertexVBO = buffers[0]
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vertexVBO)
        GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, 8 * Float.SIZE_BYTES, null, GLES20.GL_DYNAMIC_DRAW)
        GLES20.glVertexAttribPointer(positionHandle, 2, GLES20.GL_FLOAT, false, 0, 0)
        GLES20.glEnableVertexAttribArray(positionHandle)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)

        textureVBO = buffers[1]
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, textureVBO)
        GLES20.glBufferData(GLES20.GL_ARRAY_BUFFER, 8 * Float.SIZE_BYTES, null, GLES20.GL_DYNAMIC_DRAW)
        GLES20.glVertexAttribPointer(texCoordHandle, 2, GLES20.GL_FLOAT, false, 0, 0)
        GLES20.glEnableVertexAttribArray(texCoordHandle)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)
    }

    override fun drawFrame(texture: RenderTexture): Boolean {
        if (program == 0 || !texture.isValid()) return false
        if (surfaceSize.width == 0 || surfaceSize.height == 0) return false
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT or GLES20.GL_DEPTH_BUFFER_BIT)
        GLES20.glUseProgram(program)
        GLES20.glViewport(0, 0, surfaceSize.width, surfaceSize.height)

        val rot = rotationDeg.toInt() % 360
        SlarkLog.d(LOG_TAG, "drawFrame: rotationDeg=$rotationDeg, rot=$rot, texture=$texture")
        val isSwapped = rot == 90 || rot == 270
        val picW = if (isSwapped) texture.height else texture.width
        val picH = if (isSwapped) texture.width else texture.height
        val (normX, normY) = computeNormalizedScale(Size(picW, picH), surfaceSize)
        val verts = floatArrayOf(
            -normX, -normY, normX, -normY,
            -normX,  normY, normX,  normY
        )
        vertexBuffer.rewind()
        vertexBuffer.put(verts)
        vertexBuffer.rewind()
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vertexVBO)
        GLES20.glBufferSubData(GLES20.GL_ARRAY_BUFFER, 0, 8 * Float.SIZE_BYTES, vertexBuffer)

        // 通过修改 texcoord 实现旋转，避免 vertex shader 旋转与宽高比矫正冲突
        val texCoords = when (rot) {
            90  -> floatArrayOf(1f, 0f, 1f, 1f, 0f, 0f, 0f, 1f)
            180 -> floatArrayOf(1f, 1f, 0f, 1f, 1f, 0f, 0f, 0f)
            270 -> floatArrayOf(0f, 1f, 0f, 0f, 1f, 1f, 1f, 0f)
            else -> floatArrayOf(0f, 0f, 1f, 0f, 0f, 1f, 1f, 1f)
        }
        texCoordBuffer.rewind()
        texCoordBuffer.put(texCoords)
        texCoordBuffer.rewind()
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, textureVBO)
        GLES20.glBufferSubData(GLES20.GL_ARRAY_BUFFER, 0, 8 * Float.SIZE_BYTES, texCoordBuffer)
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)

        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture.textureId)
        if (texture.textureId != lastBoundTextureId) {
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
            lastBoundTextureId = texture.textureId
        }
        GLES20.glUniform1i(textureHandle, 0)

        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
        GLES20.glUseProgram(0)
        return true
    }

    private fun computeNormalizedScale(picSize: Size, surfaceSize: Size): Pair<Float, Float> {
        val contentSize = makeAspectRatioSize(picSize, surfaceSize)
        val scaleX = contentSize.width.toFloat() / surfaceSize.width
        val scaleY = contentSize.height.toFloat() / surfaceSize.height
        return if (scaleX > scaleY) Pair(1.0f, scaleY / scaleX)
               else Pair(scaleX / scaleY, 1.0f)
    }

    override fun setRotation(rotation: Double) {
        SlarkLog.i(LOG_TAG, "setRotation: $rotation")
        this.rotationDeg = rotation
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
        lastBoundTextureId = 0
    }

    companion object {
        const val LOG_TAG = "GLPreviewRenderer"
        private val VERTEX_SHADER = """
            attribute vec4 aPosition;
            attribute vec2 aTexCoord;
            varying vec2 vTexCoord;
            void main() {
                gl_Position = aPosition;
                vTexCoord = aTexCoord;
            }
        """
        private val FRAGMENT_SHADER = """
            precision mediump float;
            varying vec2 vTexCoord;
            uniform sampler2D uTexture;
            void main() { gl_FragColor = texture2D(uTexture, vTexCoord); }
        """
    }
}
