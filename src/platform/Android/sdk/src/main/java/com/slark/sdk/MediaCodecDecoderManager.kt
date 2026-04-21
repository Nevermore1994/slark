
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import java.util.concurrent.ConcurrentHashMap

class MediaCodecDecoderManager {
    private val decoders = ConcurrentHashMap<String, MediaCodecDecoder>()

    fun register(decoder: MediaCodecDecoder): String {
        val decoderId = decoder.decoderId
        decoders[decoderId] = decoder
        return decoderId
    }

    fun get(decoderId: String): MediaCodecDecoder? {
        return decoders[decoderId]
    }

    fun contains(decoderId: String): Boolean {
        return decoders.containsKey(decoderId)
    }

    fun release(decoderId: String) {
        decoders[decoderId]?.release()
        decoders.remove(decoderId)
        SlarkLog.i(LOG_TAG, "release: $decoderId")
    }

    fun releaseAll() {
        decoders.forEach { (_, decoder) -> decoder.release() }
        decoders.clear()
    }

    companion object {
        const val LOG_TAG = "DecoderManager"
    }
}
