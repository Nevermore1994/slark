
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

interface IPreviewRenderer {
    fun init()
    fun drawFrame(texture: RenderTexture): Boolean
    fun setRotation(rotation: Double)
    fun setSurfaceSize(width: Int, height: Int)
    fun release()
}
