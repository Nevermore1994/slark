package com.slark.sdk

import android.graphics.SurfaceTexture
import android.media.MediaFormat
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.opengl.Matrix
import android.util.Size
import java.nio.ByteBuffer
import java.nio.ByteOrder


class TextureRender {
    private var program: Int = 0
    private val vbo = IntArray(1)
    private var textureId: Int = 0
    private var positionLocation: Int = 0
    private var texCoordLocation: Int = 0
    private var texMatrixLocation: Int = 0
    private var textureLocation: Int = 0
    private var mvpMatrixLocation: Int = 0

    init {
        do {
            program = createGLProgram(VERTEX_SHADER, FRAGMENT_SHADER)
            if (program == 0) {
                SlarkLog.e(LOG_TAG, "create program failed")
                break
            }
            GLES20.glGenBuffers(1, vbo, 0)
            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vbo[0])

            val vertexBuffer = ByteBuffer
                .allocateDirect(kVertices.size * 4)
                .order(ByteOrder.nativeOrder())
                .asFloatBuffer()
                .put(kVertices)
            vertexBuffer.position(0)

            GLES20.glBufferData(
                GLES20.GL_ARRAY_BUFFER,
                kVertices.size * 4,
                vertexBuffer,
                GLES20.GL_STATIC_DRAW
            )

            GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)

            val textureIds = IntArray(1)
            GLES20.glGenTextures(1, textureIds, 0)
            textureId = textureIds[0]
            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId)
            if (!checkGLStatus("bind texture")) break

            GLES20.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
                GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR.toFloat())
            GLES20.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
                GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR.toFloat())
            GLES20.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
                GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE.toFloat())
            GLES20.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
                GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE.toFloat())
            if (!checkGLStatus("set texture parameter")) break

            texMatrixLocation = GLES20.glGetUniformLocation(program, "uTexMatrix")
            mvpMatrixLocation = GLES20.glGetUniformLocation(program, "uMVPMatrix")
            textureLocation = GLES20.glGetUniformLocation(program, "uTexture")
            positionLocation = GLES20.glGetAttribLocation(program, "a_position")
            texCoordLocation = GLES20.glGetAttribLocation(program, "a_texCoord")
        } while(false)
    }

    fun getTextureId(): Int {
        return textureId
    }

    fun isValid(): Boolean{
        return program != 0
    }

    fun release() {
        if (program != 0) {
            GLES20.glDeleteProgram(program)
            program = 0
        }
        if (vbo[0] != 0) {
            GLES20.glDeleteBuffers(1, vbo, 0)
            vbo[0] = 0
        }
        if (textureId != 0) {
            val textureIds = intArrayOf(textureId)
            GLES20.glDeleteTextures(1, textureIds, 0)
        }
    }

    fun drawFrame(texture: SurfaceTexture, format: MediaFormat, size: Size) {
        if (!isValid()) {
            SlarkLog.e(LOG_TAG, "program is not valid")
            return
        }
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)

        GLES20.glUseProgram(program)
        if (!checkGLStatus("use program")) return

        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, vbo[0])
        GLES20.glEnableVertexAttribArray(positionLocation)
        GLES20.glVertexAttribPointer(
            0, 2, GLES20.GL_FLOAT, false,
            4 * 4, 0
        )

        GLES20.glEnableVertexAttribArray(texCoordLocation)
        GLES20.glVertexAttribPointer(
            1, 2, GLES20.GL_FLOAT, false,
            4 * 4, 2 * 4
        )
        GLES20.glBindBuffer(GLES20.GL_ARRAY_BUFFER, 0)

        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId)
        GLES20.glUniform1i(textureLocation, 0)

        val displayAspect = size.width.toFloat() / size.height.toFloat()
        val videoAspect = format.getInteger(MediaFormat.KEY_WIDTH).toFloat() /
            format.getInteger(MediaFormat.KEY_HEIGHT).toFloat()
        val mvpMatrix = FloatArray(16)
        Matrix.setIdentityM(mvpMatrix, 0)
        if (displayAspect > videoAspect) {
            Matrix.scaleM(mvpMatrix, 0, videoAspect/displayAspect, -1f, 1f)
        } else {
            Matrix.scaleM(mvpMatrix, 0, 1f, displayAspect/videoAspect * -1.0f, 1f)
        }
        GLES20.glUniformMatrix4fv(mvpMatrixLocation, 1, false, mvpMatrix, 0)

        val matrix = FloatArray(16)
        texture.getTransformMatrix(matrix)
        GLES20.glUniformMatrix4fv(texMatrixLocation, 1, false, matrix, 0)

        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)

        GLES20.glUseProgram(0)
        GLES20.glFlush()
        if (!checkGLStatus("drawFrame")) {
            SlarkLog.e(LOG_TAG, "GL error in drawFrame")
            return
        }
    }

    companion object {
        const val LOG_TAG = "TextureRender"
        val VERTEX_SHADER = """
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

        val FRAGMENT_SHADER = """
            #version 100
            #extension GL_OES_EGL_image_external : require
            precision mediump float;

            varying vec2 v_texCoord;

            uniform samplerExternalOES uTexture;

            void main() {
                gl_FragColor = texture2D(uTexture, v_texCoord);
            }
        """.trimIndent()

        // x, y,   u, v
        private val kVertices = floatArrayOf(
            -1.0f, -1.0f,   0.0f, 1.0f,
            1.0f, -1.0f,   1.0f, 1.0f,
            -1.0f,  1.0f,   0.0f, 0.0f,
            1.0f,  1.0f,   1.0f, 0.0f,
        )
    }
}