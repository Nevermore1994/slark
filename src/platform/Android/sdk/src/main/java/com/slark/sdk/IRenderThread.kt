
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import android.view.Surface

/ Abstraction for render threads supporting different graphics backends
interface IRenderThread {
    var playerId: String
    fun start()
    fun render(textureId: Int, width: Int, height: Int)
    fun render(texture: RenderTexture)
    fun shutdown()
    fun setRotation(rotation: Int)
    fun setRenderSize(width: Int, height: Int)
    fun rebuildSurface()
}

/ Factory for creating render threads based on graphics backend
object RenderThreadFactory {
    fun create(
        backend: GraphicsBackend,
        surface: Surface,
        width: Int,
        height: Int
    ): IRenderThread = when (backend) {
        GraphicsBackend.OpenGL -> GLRenderThread(surface, width, height)
    }
}
