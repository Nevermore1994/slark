
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
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
