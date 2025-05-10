package com.slark.sdk

import android.opengl.GLSurfaceView
import javax.microedition.khronos.egl.EGL10
import javax.microedition.khronos.egl.EGLDisplay
import javax.microedition.khronos.egl.EGLContext

class SharedContextFactory(private val sharedContext: EGLContext?) : GLSurfaceView.EGLContextFactory {
    override fun createContext(egl: EGL10, display: EGLDisplay, eglConfig: javax.microedition.khronos.egl.EGLConfig): EGLContext {
        val attribList = intArrayOf(0x3098, 2, EGL10.EGL_NONE) // EGL_CONTEXT_CLIENT_VERSION, 2
        return egl.eglCreateContext(display, eglConfig, sharedContext, attribList)
    }

    override fun destroyContext(egl: EGL10, display: EGLDisplay, context: EGLContext) {
        egl.eglDestroyContext(display, context)
    }
}
