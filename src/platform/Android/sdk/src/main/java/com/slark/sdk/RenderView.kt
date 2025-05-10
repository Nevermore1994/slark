package com.slark.sdk

import android.annotation.SuppressLint
import android.content.Context
import android.opengl.GLSurfaceView
import javax.microedition.khronos.egl.EGLContext

@SuppressLint("ViewConstructor")
class RenderView(sharedContext: EGLContext,
                 context: Context) : GLSurfaceView(context) {
    private var previewRender: PreviewRender? = null

    init {
        setEGLContextClientVersion(2)
        setEGLContextFactory(SharedContextFactory(sharedContext))
        renderMode = RENDERMODE_WHEN_DIRTY
    }

    fun setPreviewRender(render: PreviewRender) {
        previewRender = render
        setRenderer(previewRender)
    }

    fun release() {
        queueEvent{
            previewRender?.release()
            previewRender = null
        }
        setRenderer(null)
    }

    fun requestRender(textureId: Int) {
        queueEvent{
            previewRender?.sharedTextureId = textureId
        }
        requestRender()
    }

}