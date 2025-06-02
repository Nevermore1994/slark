package com.slark.sdk

import android.opengl.EGLContext
import android.opengl.GLES20
import android.opengl.EGL14
import com.slark.sdk.TextureRender.Companion.LOG_TAG

fun Long.toPointer(): EGLContext = this.toRawPtr()
@Suppress("UNCHECKED_CAST")
fun <T> Long.toRawPtr(): T = java.lang.Long.valueOf(this) as T

fun checkGLStatus(tag: String): Boolean {
    var count = 0
    do {
        val error = GLES20.glGetError()
        if (error == GLES20.GL_NO_ERROR) {
            break
        }
        SlarkLog.e("GLStatus", tag +", error:${error.toInt()}")
        count++
    } while(count < 10)
    return count == 0
}

fun checkEGLContext(): Boolean {
    val egl = EGL14.eglGetCurrentContext()
    val isValid = egl != EGL14.EGL_NO_CONTEXT
    return isValid
}

fun loadShader(type: Int, shaderStr: String): Int {
    val shader = GLES20.glCreateShader(type)
    checkGLStatus("create shader ${type}")
    if (shader == 0) {
        return 0
    }
    GLES20.glShaderSource(shader, shaderStr)
    GLES20.glCompileShader(shader)

    val status = intArrayOf(1)
    GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, status, 0)
    if (status[0] == 0) {
        checkGLStatus("compile shader:${type}")
        SlarkLog.e(LOG_TAG, "shader source:${shaderStr}")
        GLES20.glDeleteShader(shader)
        return 0
    }
    return shader
}

fun createGLProgram(vertexStr: String, fragmentStr: String): Int {
    if (!checkEGLContext()) {
        SlarkLog.e(LOG_TAG, "EGL context is not valid")
        return 0
    }
    val vertexShader = loadShader(GLES20.GL_VERTEX_SHADER, vertexStr)
    if (vertexShader == 0) {
        return 0
    }
    val fragmentShader = loadShader(GLES20.GL_FRAGMENT_SHADER, fragmentStr)
    if (fragmentShader == 0) {
        return 0
    }

    var program = GLES20.glCreateProgram()
    if (!checkGLStatus("create program")) {
        return 0
    }
    GLES20.glAttachShader(program, vertexShader)
    GLES20.glAttachShader(program, fragmentShader)
    GLES20.glLinkProgram(program)
    if (!checkGLStatus("link program")) {
        program = 0
    }
    GLES20.glDeleteShader(vertexShader)
    GLES20.glDeleteShader(fragmentShader)
    return program
}