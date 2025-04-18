package com.slark.sdk

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import com.slark.sdk.SlarkLog
import java.nio.ByteBuffer

fun Int.hasFlag(flag: Int) = (this and flag) == flag

class MediaCodecDecoder(private val mediaInfo: String, private val format: MediaFormat) {
    private var decoder: MediaCodec? = null
    private var isRunning: Boolean = false

    enum class Action {
        FLUSH,
    }

    enum class ErrorCode {
        SUCCESS,
        UNKNOWN,
        NOT_PROCESSED,
        NOT_START,
        INPUT_DATA_ERROR,
        INPUT_DATA_TOO_LARGE,
        NOT_FOUND_DECODER,
        ERROR_DECODER
    }

    init {
        decoder = MediaCodec.createDecoderByType(mediaInfo)
        decoder?.configure(format, null, null, 0)
        decoder?.start()
        isRunning = true
    }

    /// BUFFER_FLAG_CODEC_CONFIG: 2
    /// BUFFER_FLAG_DECODE_ONLY: 32
    /// BUFFER_FLAG_END_OF_STREAM: 4
    /// BUFFER_FLAG_KEY_FRAME: 1
    /// BUFFER_FLAG_PARTIAL_FRAME: 8
    fun sendPacket(byteBuffer: ByteArray?, presentationTimeUs: Long, flag: Int): Int {
        var errorCode: ErrorCode = ErrorCode.UNKNOWN
        if (!isRunning) {
            errorCode = ErrorCode.NOT_START
            return errorCode.ordinal
        }

        execute() {
            val inputBufferIndex =
                decoder?.dequeueInputBuffer(10_000) ?: return@execute //10 ms
            if (inputBufferIndex == -1) {
                //TODO: add log
                errorCode = ErrorCode.NOT_PROCESSED
                return@execute
            }
            val inputBuffer = decoder!!.getInputBuffer(inputBufferIndex)!!
            inputBuffer.clear()
            if (flag.hasFlag (MediaCodec.BUFFER_FLAG_END_OF_STREAM)) {
                decoder?.queueInputBuffer(inputBufferIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
            } else {
                if (byteBuffer == null || byteBuffer.isEmpty()) {
                    errorCode = ErrorCode.INPUT_DATA_ERROR
                    return@execute
                }
                if (inputBuffer.capacity() < byteBuffer.size) {
                    //TODO: add log
                    errorCode = ErrorCode.INPUT_DATA_TOO_LARGE
                    return@execute
                }
                inputBuffer.put(byteBuffer)
                decoder?.queueInputBuffer(inputBufferIndex, 0, byteBuffer.size, presentationTimeUs, 0)
            }
            errorCode = ErrorCode.SUCCESS
        }

        execute {
            val info = MediaCodec.BufferInfo()
            val outputBufferIndex =
                decoder?.dequeueOutputBuffer(info, 2_000) ?: return@execute //2 ms
            if (outputBufferIndex == -1) {
                SlarkLog.i(LOG_TAG, "get output buffer time out")
                return@execute
            }

            val outputBuffer =
                decoder?.getOutputBuffer(outputBufferIndex) ?: return@execute
            val rawData = ByteArray(info.size)
            outputBuffer.get(rawData)
            processRawData(decoder?.hashCode().toString(), rawData, info.presentationTimeUs)
            return@execute
        }
        return errorCode.ordinal
    }

    fun release() {
        isRunning = false
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
            SlarkLog.e(LOG_TAG, "catch Exception:{}", e)
        }
    }

    private external fun processRawData(decoderId: String, byteBuffer: ByteArray, presentationTimeUs: Long)

    companion object {
        public const val LOG_TAG = "MediaCodec"
        private val decoders = HashMap<String, MediaCodecDecoder>()

        @JvmStatic @Synchronized
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

        @JvmStatic @Synchronized
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

            val decoder = MediaCodecDecoder(mediaInfo, format)
            val decoderId = decoder.hashCode().toString()
            decoders[decoderId] = decoder
            return decoderId
        }

        @JvmStatic @Synchronized
        fun releaseDecoder(decoderId: String) {
            if (!decoders.contains(decoderId)) {
                return
            }
            decoders[decoderId]?.release()
            decoders.remove(decoderId)
        }

        @JvmStatic @Synchronized
        fun sendPacket(decoderId: String, byteBuffer: ByteArray?, pts: Long, flag: Int): Int {
            if (!decoders.contains(decoderId)) {
                return ErrorCode.NOT_FOUND_DECODER.ordinal
            }
            val res = decoders[decoderId]?.sendPacket(byteBuffer, pts, flag)
            return res ?: ErrorCode.ERROR_DECODER.ordinal
        }

        @JvmStatic @Synchronized
        fun doAction(decoderId: String, action: Action) {
            when (action) {
                Action.FLUSH -> decoders[decoderId]?.flush()
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