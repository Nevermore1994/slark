package com.slark.sdk


import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.util.Size
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
    private var rotationHandle: Int = 0
    private lateinit var texturePos: FloatBuffer
    private var size: Size = Size(0, 0)
    @Volatile
    var sharedTexture: RenderTexture = RenderTexture.default()
    @Volatile
    var rotation: Double = 0.0

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        do {
            program = createGLProgram(VERTEX_SHADER_CODE, FRAGMENT_SHADER_CODE).also { program ->
                positionHandle = GLES20.glGetAttribLocation(program, "aPosition")
                textureHandle = GLES20.glGetUniformLocation(program, "uTexture")
                texCoordHandle = GLES20.glGetAttribLocation(program, "aTexCoord")
                rotationHandle = GLES20.glGetUniformLocation(program, "uRotation")
            }
            if (program == 0) {
                SlarkLog.e(LOG_TAG, "create program failed")
                break
            }
            texturePos = ByteBuffer.allocateDirect(textureData.size * Float.SIZE_BYTES)
                .order(ByteOrder.nativeOrder())
                .asFloatBuffer().apply {
                    put(textureData)
                    position(0)
                }
        } while(false)
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        GLES20.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
        size = Size(width, height)
    }

    fun drawFrame(texture: RenderTexture): Boolean {
        if (program == 0) {
            SlarkLog.e(LOG_TAG, "program is invalid")
            return false
        }
        sharedTexture = texture
        if (!sharedTexture.isValid()) {
            SlarkLog.e(LOG_TAG, "textureId is invalid")
            return false
        }
        SlarkLog.i(LOG_TAG, "draw video frame:${texture.textureId}")
        onDrawFrame(null)
        sharedTexture = RenderTexture.default()
        return true
    }

    override fun onDrawFrame(gl: GL10?) {
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT or GLES20.GL_DEPTH_BUFFER_BIT)
        GLES20.glUseProgram(program)
        GLES20.glViewport(0, 0, size.width, size.height)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)

        //val bitmap = readTextureToBitmap(sharedTexture) //for debug
        val pictureSize = Size(sharedTexture.width, sharedTexture.height)
        val contentSize = makeAspectRatioSize(pictureSize, size)
        var normalizedSize = Pair(0.0f, 0.0f)
        val corpScale = Pair(contentSize.width.toFloat() / size.width, contentSize.height.toFloat() / size.height)
        normalizedSize = if (corpScale.first > corpScale.second) {
            Pair(1.0f, corpScale.second / corpScale.first)
        } else {
            Pair(corpScale.first / corpScale.second, 1.0f)
        }

        val vertexPos = floatArrayOf(
            -normalizedSize.first, -normalizedSize.second,
            normalizedSize.first, -normalizedSize.second,
            -normalizedSize.first, normalizedSize.second,
            normalizedSize.first, normalizedSize.second
        )
        SlarkLog.i(LOG_TAG, "normalizedSize: $normalizedSize, size: $size, pictureSize: $pictureSize")
        val vertexPosBuffer = ByteBuffer.allocateDirect(vertexPos.size * Float.SIZE_BYTES)
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer().apply {
                put(vertexPos)
                position(0)
            }
        GLES20.glVertexAttribPointer(positionHandle, 2, GLES20.GL_FLOAT, false, 0, vertexPosBuffer)
        GLES20.glEnableVertexAttribArray(positionHandle)

        texturePos.position(0)
        GLES20.glVertexAttribPointer(texCoordHandle, 2, GLES20.GL_FLOAT, false, 0, texturePos)
        GLES20.glEnableVertexAttribArray(texCoordHandle)

        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, sharedTexture.textureId)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glUniform1i(textureHandle, 0)
        GLES20.glUniform1f(rotationHandle, Math.toRadians(0.0).toFloat())
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)

        checkGLStatus("render frame")
        GLES20.glDisableVertexAttribArray(positionHandle)
        GLES20.glDisableVertexAttribArray(texCoordHandle)
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0)
        SlarkLog.i(LOG_TAG, "draw frame success")
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
            uniform float uRotation;
            varying vec2 vTexCoord;
            void main() {
                mat4 rotationMatrix = mat4(cos(uRotation), -sin(uRotation), 0.0, 0.0,
                                          sin(uRotation),  cos(uRotation), 0.0, 0.0,
                                          0.0, 0.0, 1.0, 0.0,
                                          0.0, 0.0, 0.0, 1.0);
                gl_Position = rotationMatrix * aPosition;
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

        //fbo
        val textureData = floatArrayOf(
            0f, 0f,
            1f, 0f,
            0f, 1f,
            1f, 1f,
        )
    }

}