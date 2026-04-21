
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import java.nio.ByteBuffer

object DecoderJNIBridge {
    @JvmStatic
    fun onRawData(decoderId: String, data: ByteBuffer, presentationTimeUs: Long, isCompleted: Boolean) {
        if (data.hasArray()) {
            processRawData(decoderId, data.array(), presentationTimeUs, isCompleted)
        } else {
            val bytes = ByteArray(data.remaining())
            data.get(bytes)
            processRawData(decoderId, bytes, presentationTimeUs, isCompleted)
        }
    }

    private external fun processRawData(decoderId: String, byteBuffer: ByteArray, presentationTimeUs: Long, isCompleted: Boolean)
}
