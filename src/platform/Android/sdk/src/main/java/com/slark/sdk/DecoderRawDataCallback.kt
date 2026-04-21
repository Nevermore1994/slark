
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import java.nio.ByteBuffer

fun interface DecoderRawDataCallback {
    fun onRawData(decoderId: String, data: ByteBuffer, presentationTimeUs: Long, isCompleted: Boolean)
}
