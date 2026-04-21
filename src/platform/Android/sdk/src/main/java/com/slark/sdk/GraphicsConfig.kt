
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
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
