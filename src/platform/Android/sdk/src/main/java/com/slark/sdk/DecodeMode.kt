
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//


package com.slark.sdk

enum class DecodeMode(val value: Int) {
    Texture(0),
    ByteBuffer(1);

    companion object {
        fun fromInt(value: Int): DecodeMode {
            return entries.firstOrNull { it.value == value } ?: Texture
        }
    }
}
