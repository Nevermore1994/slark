
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

data class AudioDecoderConfig(
    val sampleRate: Int,
    val channels: Int,
    val profile: Int,
    val samplingFreqIndex: Int,
    val audioObjectTypeExt: Int,
    val samplingFreqIndexExt: Int
)
