
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
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
