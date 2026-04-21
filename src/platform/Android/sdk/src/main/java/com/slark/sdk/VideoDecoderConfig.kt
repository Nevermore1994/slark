
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

data class VideoDecoderConfig(
    val width: Int,
    val height: Int,
    val profile: Int,
    val level: Int,
    val decodeMode: DecodeMode = DecodeMode.Texture
)
