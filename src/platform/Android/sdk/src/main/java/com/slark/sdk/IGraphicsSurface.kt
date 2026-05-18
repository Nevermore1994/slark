
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//


package com.slark.sdk

import android.view.Surface

interface IGraphicsSurface {
    val surface: Surface?
    val isValid: Boolean

    fun awaitFrame(waitTime: Long): Pair<Boolean, Long>
    fun drawFrame(format: android.media.MediaFormat, size: android.util.Size): Boolean
    fun release()
}
