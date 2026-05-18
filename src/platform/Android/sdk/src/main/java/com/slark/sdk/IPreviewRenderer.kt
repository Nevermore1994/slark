
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//


package com.slark.sdk

interface IPreviewRenderer {
    fun init()
    fun drawFrame(texture: RenderTexture): Boolean
    fun setRotation(rotation: Double)
    fun setSurfaceSize(width: Int, height: Int)
    fun release()
}
