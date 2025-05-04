package com.slark.sdk

import android.graphics.SurfaceTexture
import android.media.MediaFormat
import android.opengl.GLES11Ext
import android.opengl.GLES30
import android.opengl.Matrix
import android.util.Size
import java.nio.ByteBuffer
import java.nio.ByteOrder


class TextureRender {
    private var program: Int = 0
    private val vao = IntArray(1)
    private val vbo = IntArray(1)
    private var textureId: Int = 0
    private var texMatrixLocation: Int = 0
    private var textureLocation: Int = 0
    private var mvpMatrixLocation: Int = 0

    init {
        program = createProgram(VERTEX_SHADER, FRAGMENT_SHADER)
        GLES30.glGenVertexArrays(1, vao, 0)
        GLES30.glGenBuffers(1, vbo, 0)

        GLES30.glBindVertexArray(vao[0])
        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, vbo[0])
        val vertexBuffer = ByteBuffer
            .allocateDirect(kVertices.size * 4)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
            .put(kVertices)
        vertexBuffer.position(0)

        GLES30.glBufferData(
            GLES30.GL_ARRAY_BUFFER,
            kVertices.size * 4,
            vertexBuffer,
            GLES30.GL_STATIC_DRAW
        )

        GLES30.glVertexAttribPointer(0, 2, GLES30.GL_FLOAT,
            false, 4 * 4, 0)
        GLES30.glEnableVertexAttribArray(0)


        GLES30.glVertexAttribPointer(1, 2, GLES30.GL_FLOAT,
            false, 4 * 4, 2 * 4)
        GLES30.glEnableVertexAttribArray(1)


        GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, 0)
        GLES30.glBindVertexArray(0)

        val textureIds = intArrayOf(0)
        GLES30.glGenTextures(1, textureIds, 0)
        textureId = textureIds[0]
        GLES30.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId)
        checkGLStatus("bind texture")

        GLES30.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR.toFloat())
        GLES30.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR.toFloat())
        GLES30.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE.toFloat())
        GLES30.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
            GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE.toFloat())
        checkGLStatus("set texture parameter")
        texMatrixLocation = GLES30.glGetUniformLocation(program, "uTexMatrix")
        mvpMatrixLocation = GLES30.glGetUniformLocation(program, "uMVPMatrix")
        textureLocation = GLES30.glGetUniformLocation(program, "uTexture")
    }

    fun getTextureId(): Int {
        return textureId
    }

    fun isValid(): Boolean{
        return program != 0
    }

    fun release() {
        if (program != 0) {
            GLES30.glDeleteProgram(program)
            program = 0
        }
        if (vao[0] != 0) {
            GLES30.glDeleteVertexArrays(1, vao, 0)
            GLES30.glDeleteBuffers(1, vbo, 0)
            vao[0] = 0
            vbo[0] = 0
        }
        if (textureId != 0) {
            val textureIds = intArrayOf(textureId)
            GLES30.glDeleteTextures(1, textureIds, 0)
        }
    }

    fun drawFrame(texture: SurfaceTexture, format: MediaFormat, size: Size) {
        GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)

        GLES30.glUseProgram(program)
        checkGLStatus("use program")

        GLES30.glBindVertexArray(vao[0])

        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId)
        GLES30.glUniform1i(textureLocation, 0)

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
        GLES30.glUniformMatrix4fv(mvpMatrixLocation, 1, false, mvpMatrix, 0)

        val matrix = FloatArray(16)
        texture.getTransformMatrix(matrix)
        GLES30.glUniformMatrix4fv(texMatrixLocation, 1, false, matrix, 0)

        GLES30.glDrawArrays(GLES30.GL_TRIANGLE_STRIP, 0, 4)

        GLES30.glBindVertexArray(0)
        GLES30.glUseProgram(0)
        GLES30.glFlush()
        if (!checkGLStatus("drawFrame")) {
            SlarkLog.e(LOG_TAG, "GL error in drawFrame")
            return
        }
    }

    private fun loadShader(type: Int, shaderStr: String): Int {
        val shader = GLES30.glCreateShader(type)
        checkGLStatus("create shader ${type}")
        if (shader == 0) {
            return 0
        }
        GLES30.glShaderSource(shader, shaderStr)
        GLES30.glCompileShader(shader)

        val status = intArrayOf(1)
        GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, status, 0)
        if (status[0] == 0) {
            checkGLStatus("compile shader:${type}")
            SlarkLog.e(LOG_TAG, "shader source:${shaderStr}")
            GLES30.glDeleteShader(shader)
            return 0
        }
        return shader
    }

    private fun createProgram(vertexStr: String, fragmentStr: String): Int {
        val vertexShader = loadShader(GLES30.GL_VERTEX_SHADER, vertexStr)
        if (vertexShader == 0) {
            return 0
        }
        val fragmentShader = loadShader(GLES30.GL_FRAGMENT_SHADER, fragmentStr)
        if (fragmentShader == 0) {
            return 0
        }

        var program = GLES30.glCreateProgram()
        if (!checkGLStatus("create program")) {
            return 0
        }
        GLES30.glAttachShader(program, vertexShader)
        GLES30.glAttachShader(program, fragmentShader)
        GLES30.glLinkProgram(program)
        if (!checkGLStatus("link program")) {
            program = 0
        }
        GLES30.glDeleteShader(vertexShader)
        GLES30.glDeleteShader(fragmentShader)
        return program
    }

    companion object {
        const val LOG_TAG = "TextureRender"
        val VERTEX_SHADER = """
            #version 300 es
            precision mediump float;
            
            layout(location = 0) in vec4 a_position;
            layout(location = 1) in vec2 a_texCoord;
            
            uniform mat4 uMVPMatrix;
            uniform mat4 uTexMatrix;
            
            out vec2 v_texCoord;
            
            void main() {
                gl_Position = uMVPMatrix * a_position;
                vec4 texCoord = uTexMatrix * vec4(a_texCoord, 0.0, 1.0);
                v_texCoord = texCoord.xy;
            }
        """.trimIndent()

        val FRAGMENT_SHADER = """
            #version 300 es
            #extension GL_OES_EGL_image_external_essl3 : require
            precision mediump float;

            in vec2 v_texCoord;
            layout(location = 0) out vec4 outColor;

            uniform samplerExternalOES uTexture;

            void main() {
                outColor = texture(uTexture, v_texCoord);
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