
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

object JNISignatureVerifier {
    private const val LOG_TAG = "JNISignatureVerifier"

    fun verify(): Boolean {
        val results = listOf(
            verifyVideoDecoderConfig(),
            verifyAudioDecoderConfig(),
            verifyDecodeFrameResult()
        )
        val allPassed = results.all { it }
        if (allPassed) {
            SlarkLog.i(LOG_TAG, "JNI signature verification passed")
        } else {
            SlarkLog.e(LOG_TAG, "JNI signature verification FAILED")
        }
        return allPassed
    }

    private fun verifyVideoDecoderConfig(): Boolean {
        return try {
            val config = VideoDecoderConfig(1920, 1080, 1, 256, DecodeMode.Texture)
            val fields = listOf(
                "width" to config.width,
                "height" to config.height,
                "profile" to config.profile,
                "level" to config.level,
                "decodeMode" to config.decodeMode
            )
            fields.forEach { (name, value) ->
                if (value == null) {
                    SlarkLog.e(LOG_TAG, "VideoDecoderConfig field '$name' is null")
                    return false
                }
            }
            true
        } catch (e: Exception) {
            SlarkLog.e(LOG_TAG, "VideoDecoderConfig verification failed: ${e.message}")
            false
        }
    }

    private fun verifyAudioDecoderConfig(): Boolean {
        return try {
            val config = AudioDecoderConfig(44100, 2, 2, 4, 5, 3)
            val fields = listOf(
                "sampleRate" to config.sampleRate,
                "channels" to config.channels,
                "profile" to config.profile,
                "samplingFreqIndex" to config.samplingFreqIndex,
                "audioObjectTypeExt" to config.audioObjectTypeExt,
                "samplingFreqIndexExt" to config.samplingFreqIndexExt
            )
            fields.forEach { (name, value) ->
                if (value == null) {
                    SlarkLog.e(LOG_TAG, "AudioDecoderConfig field '$name' is null")
                    return false
                }
            }
            true
        } catch (e: Exception) {
            SlarkLog.e(LOG_TAG, "AudioDecoderConfig verification failed: ${e.message}")
            false
        }
    }

    private fun verifyDecodeFrameResult(): Boolean {
        return try {
            val result = DecodeFrameResult(isAvailable = true, isCompleted = false, pts = 12345L)
            if (!result.isAvailable || result.isCompleted || result.pts != 12345L) {
                SlarkLog.e(LOG_TAG, "DecodeFrameResult field verification failed")
                return false
            }
            true
        } catch (e: Exception) {
            SlarkLog.e(LOG_TAG, "DecodeFrameResult verification failed: ${e.message}")
            false
        }
    }
}
