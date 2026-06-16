//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.sdk

class VulkanPreviewRenderer : IPreviewRenderer {

    override fun init() {
    }

    override fun drawFrame(texture: RenderTexture): Boolean {
        return true
    }

    override fun setRotation(rotation: Double) {
        SlarkLog.i(LOG_TAG, "setRotation: $rotation (delegated to C++ via setVulkanRotation JNI)")
    }

    override fun setSurfaceSize(width: Int, height: Int) {
    }

    override fun release() {
    }

    companion object {
        private const val LOG_TAG = "VulkanPreviewRenderer"
    }
}
