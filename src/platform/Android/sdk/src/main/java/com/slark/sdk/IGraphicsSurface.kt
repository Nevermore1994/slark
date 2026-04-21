
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
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
