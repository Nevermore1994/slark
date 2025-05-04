package com.slark.sdk

import android.opengl.GLES30

fun checkGLStatus(tag: String): Boolean {
    var count = 0
    do {
        val error = GLES30.glGetError()
        if (error == GLES30.GL_NO_ERROR) {
            break
        }
        SlarkLog.e("GLStatus", tag +", error:${error},")
        count++
    } while(count < 10)
    return count == 0
}