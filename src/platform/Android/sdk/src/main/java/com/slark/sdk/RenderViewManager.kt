package com.slark.sdk

import java.util.concurrent.ConcurrentHashMap

class RenderViewManager {
    private val renderViewMap = ConcurrentHashMap<String, RenderView>()

    fun addRenderView(key: String, renderView: RenderView) {
        renderViewMap[key] = renderView
    }

    fun removeRenderView(key: String) {
        renderViewMap.remove(key)
    }

    fun getRenderView(key: String): RenderView? {
        return renderViewMap[key]
    }
}