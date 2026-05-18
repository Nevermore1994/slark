
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//


package com.slark.sdk

enum class GraphicsBackend {
    OpenGL,
    Vulkan
}

data class GraphicsConfig(
    val backend: GraphicsBackend = GraphicsBackend.OpenGL,
    val enableDebugLayer: Boolean = false,
    val enableValidation: Boolean = false
)
