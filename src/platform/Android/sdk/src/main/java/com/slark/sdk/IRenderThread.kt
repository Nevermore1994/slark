
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//


package com.slark.sdk

import android.view.Surface

// Abstraction for render threads supporting different graphics backends.
// Design symmetric to iOS: initContext() sets up GL/Vulkan context,
// start() registers VSync, stop() unregisters VSync, shutdown() releases resources.
interface IRenderThread {
    var playerId: String
    fun initContext()
    fun start()
    fun stop()
    fun render(textureId: Int, width: Int, height: Int)
    fun render(texture: RenderTexture)
    fun shutdown()
    fun join()
    fun setRotation(rotation: Int)
    fun setRenderSize(width: Int, height: Int)
    fun rebuildSurface()
}

// Factory for creating render threads based on graphics backend
object RenderThreadFactory {
    fun create(
        backend: GraphicsBackend,
        surface: Surface,
        width: Int,
        height: Int
    ): IRenderThread = when (backend) {
        GraphicsBackend.OpenGL -> GLRenderThread(surface, width, height)
        GraphicsBackend.Vulkan -> VulkanRenderThread(surface, width, height)
    }
}
