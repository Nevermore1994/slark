
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import android.graphics.SurfaceTexture
import android.media.MediaFormat
import android.util.Size

interface ITextureRenderer {
    val textureId: Int
    val isValid: Boolean

    fun drawFrame(surfaceTexture: SurfaceTexture, format: MediaFormat, size: Size): Boolean
    fun awaitFrame(waitTime: Long): Pair<Boolean, Long>
    fun release()
}
