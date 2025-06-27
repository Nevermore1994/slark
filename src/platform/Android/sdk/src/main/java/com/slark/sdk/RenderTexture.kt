package com.slark.sdk

data class RenderTexture(val textureId: Int, val width: Int, val height: Int) {
    var isBackup: Boolean = false
    fun isValid(): Boolean {
        return textureId != 0
    }

    companion object {
        fun default(): RenderTexture {
            return RenderTexture(0, 0, 0)
        }
    }
}
