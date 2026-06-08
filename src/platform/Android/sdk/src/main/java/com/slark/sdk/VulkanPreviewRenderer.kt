//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.sdk

// Vulkan implementation of IPreviewRenderer.
// Actual rendering is delegated to C++ layer (AndroidVulkanContext).
class VulkanPreviewRenderer : IPreviewRenderer {

    override fun init() {
        // Vulkan context is created via createVulkanContext() in VulkanRenderThread.initContext()
    }

    override fun drawFrame(texture: RenderTexture): Boolean {
        // Drawing is done in C++ layer via AndroidVulkanContext::drawFrame()
        // Called from VulkanRenderThread.drawAndPresent()
        return true
    }

    override fun setRotation(rotation: Double) {
        // Rotation is handled in C++ layer
    }

    override fun setSurfaceSize(width: Int, height: Int) {
        // Surface size is handled in C++ layer via recreateSwapchain()
    }

    override fun release() {
        // Vulkan context is destroyed via destroyVulkanContext() in VulkanRenderThread.shutdown()
    }

    companion object {
        private const val LOG_TAG = "VulkanPreviewRenderer"
    }
}
