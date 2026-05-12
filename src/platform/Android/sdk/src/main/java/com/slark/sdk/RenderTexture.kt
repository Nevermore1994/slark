
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

data class RenderTexture(val textureId: Int, val width: Int, val height: Int) {
    var isBackup: Boolean = false
    fun isValid(): Boolean {
        return textureId != 0
    }

    fun asBackup(): RenderTexture {
        return copy().also { it.isBackup = true }
    }

    companion object {
        fun default(): RenderTexture {
            return RenderTexture(0, 0, 0)
        }
    }
}
