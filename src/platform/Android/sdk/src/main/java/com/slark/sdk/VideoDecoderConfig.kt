
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//


package com.slark.sdk

data class VideoDecoderConfig(
    val width: Int,
    val height: Int,
    val profile: Int,
    val level: Int,
    val decodeMode: DecodeMode = DecodeMode.Texture
)
