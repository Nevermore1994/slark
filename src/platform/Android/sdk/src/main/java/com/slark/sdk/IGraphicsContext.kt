
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import android.view.Surface

interface IGraphicsContext {
    val backend: GraphicsBackend
    val isValid: Boolean

    fun createSharedContext(): IGraphicsContext
    fun createTexture(width: Int, height: Int): ITexture
    fun createFrameBuffer(width: Int, height: Int): IFrameBuffer
    fun createTextureRenderer(): ITextureRenderer
    fun createPreviewRenderer(): IPreviewRenderer
    fun createSurface(): IGraphicsSurface
    fun swapBuffers()
    fun makeCurrent()
    fun release()

    fun onError(callback: (String) -> Unit)
}
