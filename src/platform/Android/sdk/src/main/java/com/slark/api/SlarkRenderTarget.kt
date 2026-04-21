
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.api

import android.view.Surface
import android.view.SurfaceView
import android.view.TextureView

The base class for all render targets in Slark SDK,
render target can be a SurfaceView, TextureView, or a Surface.
sealed class SlarkRenderTarget {
    data class FromSurfaceView(val view: SurfaceView) : SlarkRenderTarget()

    data class FromTextureView(val view: TextureView) : SlarkRenderTarget()

    data class FromSurface(val surface: Surface, val width: Int, val height: Int) : SlarkRenderTarget()
}

