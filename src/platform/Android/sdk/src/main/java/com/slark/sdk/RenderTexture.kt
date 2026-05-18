
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
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
