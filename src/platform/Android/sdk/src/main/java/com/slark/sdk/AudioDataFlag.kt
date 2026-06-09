//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.sdk

enum class AudioDataFlag {
    Normal,
    EndOfStream,
    Silence,
    Error;

    companion object {
        fun fromInt(value: Int): AudioDataFlag {
            return when (value) {
                0 -> Normal
                1 -> EndOfStream
                2 -> Silence
                else -> Error
            }
        }
    }
}
