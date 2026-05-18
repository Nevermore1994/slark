
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
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
