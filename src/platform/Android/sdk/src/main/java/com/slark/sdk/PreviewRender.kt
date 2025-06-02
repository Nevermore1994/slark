package com.slark.sdk


import android.opengl.GLES20
import android.opengl.GLSurfaceView
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10


class PreviewRender(): GLSurfaceView.Renderer {
    private var program: Int = 0
    private var positionHandle: Int = 0
    private var textureHandle: Int = 0
    private var texCoordHandle: Int = 0
    private lateinit var vertexBuffer: FloatBuffer
    @Volatile
    var sharedTextureId: Int = 0

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        do {
            program = createGLProgram(VERTEX_SHADER_CODE, FRAGMENT_SHADER_CODE).also { program ->
                positionHandle = GLES20.glGetAttribLocation(program, "aPosition")
                textureHandle = GLES20.glGetUniformLocation(program, "uTexture")
                texCoordHandle = GLES20.glGetAttribLocation(program, "aTexCoord")
            }
            if (program == 0) {
                SlarkLog.e(LOG_TAG, "create program failed")
                break
            }
            vertexBuffer = ByteBuffer.allocateDirect(vertexData.size * 4)
                .order(ByteOrder.nativeOrder())
                .asFloatBuffer().apply {
                    put(vertexData)
                    position(0)
                }
        } while(false)
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        GLES20.glViewport(0, 0, width, height)
        GLES20.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
    }

    override fun onDrawFrame(gl: GL10?) {
        if (program == 0) {
            SlarkLog.e(LOG_TAG, "program is invalid")
            return
        }
        if (sharedTextureId == 0) {
            SlarkLog.e(LOG_TAG, "textureId is invalid")
            return
        }
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT or GLES20.GL_DEPTH_BUFFER_BIT)
        GLES20.glUseProgram(program)

        vertexBuffer.position(0)
        GLES20.glVertexAttribPointer(positionHandle, 2, GLES20.GL_FLOAT, false, 4 * 4, vertexBuffer)
        GLES20.glEnableVertexAttribArray(positionHandle)

        vertexBuffer.position(2)
        GLES20.glVertexAttribPointer(texCoordHandle, 2, GLES20.GL_FLOAT, false, 4 * 4, vertexBuffer)
        GLES20.glEnableVertexAttribArray(texCoordHandle)

        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, sharedTextureId)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glUniform1i(textureHandle, 0)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)

        checkGLStatus("render frame")
        GLES20.glDisableVertexAttribArray(positionHandle)
        GLES20.glDisableVertexAttribArray(texCoordHandle)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
    }

    fun release() {
        if (program != 0) {
            GLES20.glDeleteProgram(program)
            program = 0
        }
    }

    companion object {
        const val LOG_TAG = "PreviewRender"

        const val VERTEX_SHADER_CODE = """
            attribute vec4 aPosition;
            attribute vec2 aTexCoord;
            varying vec2 vTexCoord;
            void main() {
                gl_Position = aPosition;
                vTexCoord = aTexCoord;
            }
        """

        const val FRAGMENT_SHADER_CODE = """
            precision mediump float;
            varying vec2 vTexCoord;
            uniform sampler2D uTexture;
            void main() {
                gl_FragColor = texture2D(uTexture, vTexCoord);
            }
        """

        val vertexData = floatArrayOf(
            // X, Y,      U, V
            -1f, -1f,     0f, 0f,
            1f, -1f,     1f, 0f,
            -1f,  1f,     0f, 1f,
            1f,  1f,     1f, 1f,
        )
    }

}