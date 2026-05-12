
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

import android.media.MediaCodecInfo
import android.media.MediaFormat
import java.nio.ByteBuffer

class MediaCodecDecoderFactory(
    private val manager: MediaCodecDecoderManager
) {
    fun createVideoDecoder(mediaInfo: String, config: VideoDecoderConfig): String {
        val format = MediaFormat.createVideoFormat(mediaInfo, config.width, config.height).apply {
            setInteger(MediaFormat.KEY_PROFILE, config.profile)
            setInteger(MediaFormat.KEY_LEVEL, config.level)
        }
        val extractorMaxSize = format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE, -1)
        if (extractorMaxSize > 0) {
            format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, extractorMaxSize)
        } else {
            format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, kDefaultVideoMaxInputSize)
        }
        val decoder = MediaCodecDecoder(mediaInfo, format, config.decodeMode)
        return manager.register(decoder)
    }

    fun createVideoDecoder(mediaInfo: String, config: VideoDecoderConfig, surface: IGraphicsSurface): String {
        val format = MediaFormat.createVideoFormat(mediaInfo, config.width, config.height).apply {
            setInteger(MediaFormat.KEY_PROFILE, config.profile)
            setInteger(MediaFormat.KEY_LEVEL, config.level)
        }
        val extractorMaxSize = format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE, -1)
        if (extractorMaxSize > 0) format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, extractorMaxSize)
        else format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, kDefaultVideoMaxInputSize)
        val decoder = MediaCodecDecoder(mediaInfo, format, config.decodeMode, graphicsSurface = surface)
        return manager.register(decoder)
    }

    fun createVideoDecoder(mediaInfo: String, config: VideoDecoderConfig, context: IGraphicsContext): String {
        val format = MediaFormat.createVideoFormat(mediaInfo, config.width, config.height).apply {
            setInteger(MediaFormat.KEY_PROFILE, config.profile)
            setInteger(MediaFormat.KEY_LEVEL, config.level)
        }
        val extractorMaxSize = format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE, -1)
        if (extractorMaxSize > 0) format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, extractorMaxSize)
        else format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, kDefaultVideoMaxInputSize)
        val decoder = MediaCodecDecoder(mediaInfo, format, config.decodeMode, graphicsContext = context)
        return manager.register(decoder)
    }

    fun createAudioDecoder(mediaInfo: String, config: AudioDecoderConfig): String {
        if (config.sampleRate <= 0 || config.channels <= 0) {
            SlarkLog.e(LOG_TAG, "invalid audio config: sampleRate=${config.sampleRate}, channels=${config.channels}")
            return ""
        }
        val format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, config.sampleRate, config.channels)
        format.setInteger(MediaFormat.KEY_AAC_PROFILE, config.profile)
        val extractorMaxSize = format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE, -1)
        if (extractorMaxSize > 0) {
            format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, extractorMaxSize)
        } else {
            format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, kDefaultAudioMaxInputSize)
        }

        val audioSpecificConfig = buildAudioSpecificConfig(
            config.profile, config.channels,
            config.samplingFreqIndex, config.audioObjectTypeExt, config.samplingFreqIndexExt
        )
        format.setByteBuffer("csd-0", ByteBuffer.wrap(audioSpecificConfig))

        val decoder = MediaCodecDecoder(MediaFormat.MIMETYPE_AUDIO_AAC, format, DecodeMode.ByteBuffer)
        val decoderId = manager.register(decoder)
        SlarkLog.i(LOG_TAG, "create audio: $decoderId")
        return decoderId
    }

    private fun buildAudioSpecificConfig(
        profile: Int, channelCount: Int,
        samplingFreqIndex: Int, extAudioType: Int, extSamplingFreqIndex: Int
    ): ByteArray {
        val config = ByteArray(4)
        if (profile != MediaCodecInfo.CodecProfileLevel.AACObjectHE &&
            profile != MediaCodecInfo.CodecProfileLevel.AACObjectHE_PS) {
            config[0] = ((profile shl 3) or (samplingFreqIndex shr 1)).toByte()
            config[1] = (((samplingFreqIndex and 0x01) shl 7) or (channelCount shl 3)).toByte()
        } else {
            config[0] = ((profile shl 3) or (samplingFreqIndex shr 1)).toByte()
            config[1] = (((samplingFreqIndex and 0x01) shl 7) or (channelCount shl 3) or (extSamplingFreqIndex shr 1)).toByte()
            config[2] = (((extSamplingFreqIndex and 0x01) shl 7) or (extAudioType shl 2)).toByte()
        }
        return config
    }

    companion object {
        const val LOG_TAG = "DecoderFactory"
        const val kDefaultVideoMaxInputSize = 1048576 * 2
        const val kDefaultAudioMaxInputSize = 4096
    }
}
