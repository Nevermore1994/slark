package com.slark.sdk

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.util.Size
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentHashMap

fun Int.hasFlag(flag: Int) = (this and flag) == flag

fun MediaFormat.isAudioFormat(): Boolean {
    return this.getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
}

fun MediaFormat.isVideoFormat(): Boolean {
    return this.getString(MediaFormat.KEY_MIME)?.startsWith("video/") == true
}

class MediaCodecDecoder(private val mediaInfo: String, private val format: MediaFormat) {
    private var decoder: MediaCodec? = null
    private var isExited: Boolean = false
    private var isStarted: Boolean = false
    private var surface: RenderSurface? = null

    enum class Action {
        Flush,
    }

    enum class ErrorCode {
        Success,
        Unknown,
        NotProcessed,
        NotStart,
        InputDataError,
        InputDataTooLarge,
        NotFoundDecoder,
        ErrorDecoder,
    }

    init {
        decoder = MediaCodec.createDecoderByType(mediaInfo)
    }

    fun start() {
        if (decoder == null) {
            SlarkLog.e(LOG_TAG, "decoder is null")
            return
        }
        if (isStarted) {
            SlarkLog.w(LOG_TAG, "decoder already started")
            return
        }
        if (isExited) {
            SlarkLog.w(LOG_TAG, "decoder already exited")
            return
        }
        if (format.isVideoFormat()) {
            surface = RenderSurface().apply {
                init()
            }
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            surface?.surface() ?.let { outputSurface ->
                decoder?.configure(format, outputSurface, null, 0)
            } ?: run {
                SlarkLog.e(LOG_TAG, "Failed to create output surface")
                throw IllegalStateException("Failed to create output surface")
            }

        } else {
            decoder?.configure(format, null, null, 0)
        }

        decoder?.start()
        isStarted = true
    }

    /// BUFFER_FLAG_CODEC_CONFIG: 2
    /// BUFFER_FLAG_DECODE_ONLY: 32
    /// BUFFER_FLAG_END_OF_STREAM: 4
    /// BUFFER_FLAG_KEY_FRAME: 1
    /// BUFFER_FLAG_PARTIAL_FRAME: 8
    fun sendPacket(byteBuffer: ByteArray?, presentationTimeUs: Long, flag: Int): Int {
        if (!isStarted) {
            start();
        }

        var errorCode: ErrorCode = ErrorCode.Unknown
        if (isExited) {
            errorCode = ErrorCode.NotStart
            return errorCode.ordinal
        }

        execute() {
            val inputBufferIndex =
                decoder?.dequeueInputBuffer(10_000) ?: return@execute //10 ms
            if (inputBufferIndex == -1) {
                SlarkLog.e(LOG_TAG, "no input buffer available")
                errorCode = ErrorCode.NotProcessed
                return@execute
            }
            val inputBuffer = decoder!!.getInputBuffer(inputBufferIndex)!!
            inputBuffer.clear()
            if (flag.hasFlag (MediaCodec.BUFFER_FLAG_END_OF_STREAM)) {
                decoder?.queueInputBuffer(inputBufferIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
            } else {
                if (byteBuffer == null || byteBuffer.isEmpty()) {
                    errorCode = ErrorCode.InputDataError
                    return@execute
                }
                if (inputBuffer.capacity() < byteBuffer.size) {
                    SlarkLog.e(LOG_TAG, "input buffer too small: ${inputBuffer.capacity()} < ${byteBuffer.size}")
                    errorCode = ErrorCode.InputDataTooLarge
                    return@execute
                }
                inputBuffer.put(byteBuffer)
                decoder?.queueInputBuffer(inputBufferIndex, 0, byteBuffer.size, presentationTimeUs, 0)
            }
            errorCode = ErrorCode.Success
        }

        execute {
            val info = MediaCodec.BufferInfo()
            val outputBufferIndex =
                decoder?.dequeueOutputBuffer(info, 2_000) ?: return@execute //2 ms
            when (outputBufferIndex) {
                MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                    SlarkLog.i(LOG_TAG, "output format changed: ${decoder?.outputFormat}")
                    val newFormat = decoder?.outputFormat
                    format
                    return@execute
                }
                MediaCodec.INFO_TRY_AGAIN_LATER -> {
                    SlarkLog.i(LOG_TAG, "no output buffer available")
                    return@execute
                }
                MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED -> {
                    SlarkLog.i(LOG_TAG, "output buffers changed")
                    @Suppress("DEPRECATION")
                    val outputBuffers = decoder?.outputBuffers
                    return@execute
                }
                else -> {
                    val outputBuffer =
                        decoder?.getOutputBuffer(outputBufferIndex) ?: return@execute
                    val rawData = ByteArray(info.size)
                    outputBuffer.get(rawData)
                    processRawData(decoder?.hashCode().toString(), rawData, info.presentationTimeUs)
                    return@execute
                }
            }
        }
        return errorCode.ordinal
    }

    fun release() {
        isExited = true
        isStarted = false
        flush()
        decoder?.release()
        decoder = null
    }

    fun flush() {
        decoder?.flush()
    }

    fun isValid(): Boolean {
        return decoder != null
    }

    private fun execute(action: () -> Unit) {
        try {
            action.invoke()
        } catch (e: Exception) {
            SlarkLog.e(LOG_TAG, "catch Exception:{}", e.message)
        }
    }

    fun requestVideoFrame(waitTime: Long, width: Int, height: Int): Long {
        surface?.let {
            val res = it.awaitFrame(waitTime)
            if (res.first) {
                it.drawFrame(format, Size(width, height))
                // set high bit to indicate frame available
                return 1L shl 63 or (res.second and 0x7FFFFFFFFFFFFFFF)
            }
        }
        return 0L
    }

    private external fun processRawData(decoderId: String, byteBuffer: ByteArray, presentationTimeUs: Long)

    companion object {
        const val LOG_TAG = "MediaCodec"
        private val decoders = ConcurrentHashMap<String, MediaCodecDecoder>()

        @JvmStatic
        fun createVideoDecoder(mediaInfo: String, width: Int, height: Int,
                               spsData: ByteArray, ppsData: ByteArray, vpsData: ByteArray): String {
            val format = MediaFormat.createVideoFormat(mediaInfo, width, height)
            val extractorMaxSize = format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE, -1)
            if (extractorMaxSize > 0) {
                format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, extractorMaxSize)
            } else {
                format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 1048576 * 2) //2MB
            }
            val csd0Buffer = ByteBuffer.wrap(spsData)
            if (vpsData.isNotEmpty()) {
                csd0Buffer.put(vpsData) //write vps
            }
            format.setByteBuffer("csd-0", ByteBuffer.wrap(spsData))
            format.setByteBuffer("csd-1", ByteBuffer.wrap(ppsData))

            val decoder = MediaCodecDecoder(mediaInfo, format)
            val decoderId = decoder.hashCode().toString()
            decoders[decoderId] = decoder
            return decoderId
        }

        @JvmStatic
        fun createAudioDecoder(mediaInfo: String, sampleRate: Int, channelCount: Int,
                               profile: Int): String {
            val format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, sampleRate, channelCount)
            format.setInteger(MediaFormat.KEY_AAC_PROFILE, profile)
            val extractorMaxSize = format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE, -1)
            if (extractorMaxSize > 0) {
                format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, extractorMaxSize)
            } else {
                format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 4096)
            }

            val samplingFreqIndex = getSamplingFreqIndex(sampleRate)
            val audioObjectType = profileToAOT(profile)
            val config = ByteArray(2)
            config[0] = ((audioObjectType shl 3) or (samplingFreqIndex shr 1)).toByte()
            config[1] = (((samplingFreqIndex and 0x01) shl 7) or (channelCount shl 3)).toByte()

            val csd0 = ByteBuffer.wrap(config)
            format.setByteBuffer("csd-0", csd0)

            val decoder = MediaCodecDecoder(MediaFormat.MIMETYPE_AUDIO_AAC, format)
            val decoderId = decoder.hashCode().toString()
            decoders[decoderId] = decoder
            SlarkLog.i(LOG_TAG, "create: $decoderId")
            return decoderId
        }

        @JvmStatic
        fun sendPacket(decoderId: String, byteBuffer: ByteArray?, presentationTimeUs: Long, flag: Int): Int {
            if (!decoders.containsKey(decoderId)) {
                SlarkLog.e(LOG_TAG, "not found decoder")
                return ErrorCode.NotFoundDecoder.ordinal
            }
            return decoders[decoderId]?.sendPacket(byteBuffer, presentationTimeUs, flag) ?: ErrorCode.NotFoundDecoder.ordinal
        }

        @JvmStatic
        fun requestVideoFrame(decoderId: String, waitTime: Long, width: Int, height: Int): Long {
            if (!decoders.containsKey(decoderId)) {
                SlarkLog.e(LOG_TAG, "not found decoder")
                return 0L
            }
            return decoders[decoderId]?.requestVideoFrame(waitTime, width, height) ?: 0L
        }

        @JvmStatic
        fun releaseDecoder(decoderId: String) {
            if (!decoders.contains(decoderId)) {
                return
            }
            decoders[decoderId]?.release()
            decoders.remove(decoderId)
            SlarkLog.i(LOG_TAG, "release: $decoderId")
        }

        @JvmStatic @Synchronized
        fun doAction(decoderId: String, action: Action) {
            when (action) {
                Action.Flush -> decoders[decoderId]?.flush()
            }
        }

        private fun getSamplingFreqIndex(sampleRate: Int): Int {
            return when (sampleRate) {
                96000 -> 0
                88200 -> 1
                64000 -> 2
                48000 -> 3
                44100 -> 4
                32000 -> 5
                24000 -> 6
                22050 -> 7
                16000 -> 8
                12000 -> 9
                11025 -> 10
                8000  -> 11
                7350  -> 12
                else -> 4 // default 44100
            }
        }

        private fun profileToAOT(profile: Int): Int {
            return when (profile) {
                MediaCodecInfo.CodecProfileLevel.AACObjectLC -> 2
                MediaCodecInfo.CodecProfileLevel.AACObjectHE -> 5
                MediaCodecInfo.CodecProfileLevel.AACObjectHE_PS -> 29
                else -> 2
            }
        }
    }

}