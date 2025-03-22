package com.slark.sdk

import android.media.MediaCodec
import android.media.MediaFormat
import android.util.Log

fun Int.hasFlag(flag: Int) = (this and flag) == flag

class MediaCodecDecoder(private val mediaInfo: String, private val format: MediaFormat) {
    private var decoder: MediaCodec? = null
    private var isRunning: Boolean = false

    enum class Action {
        FLUSH,
    }

    enum class ErrorCode {
        SUCCESS,
        NOT_PROCESSED,
        NOT_START,
        INPUT_DATA_TOO_LARGE,
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
    fun receivePacket(byteBuffer: ByteArray, presentationTimeUs: Long, flag: Int): Int {
        var errorCode: ErrorCode = ErrorCode.NOT_PROCESSED
        if (!isRunning) {
            errorCode = ErrorCode.NOT_START
            return errorCode.ordinal
        }

        executeAction {
            val inputBufferIndex =
                decoder?.dequeueInputBuffer(10_000) ?: return@executeAction //10 ms
            if (inputBufferIndex == -1) {
                //TODO: add log
                return@executeAction
            }
            val inputBuffer = decoder!!.getInputBuffer(inputBufferIndex)!!
            inputBuffer.clear()
            if (flag.hasFlag (MediaCodec.BUFFER_FLAG_END_OF_STREAM)) {
                decoder?.queueInputBuffer(inputBufferIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
            } else {
                if (inputBuffer.capacity() < byteBuffer.size) {
                    errorCode = ErrorCode.INPUT_DATA_TOO_LARGE
                    //TODO: add log
                    return@executeAction
                }
                inputBuffer.put(byteBuffer)
                decoder?.queueInputBuffer(inputBufferIndex, 0, byteBuffer.size, presentationTimeUs, 0)
            }
            errorCode = ErrorCode.SUCCESS
        }

        executeAction {
            val info = MediaCodec.BufferInfo()
            val outputBufferIndex =
                decoder?.dequeueOutputBuffer(info, 2_000) ?: return@executeAction //2 ms
            if (outputBufferIndex == -1) {
                //TODO: add log
                return@executeAction
            }

            val outputBuffer =
                decoder?.getInputBuffer(outputBufferIndex) ?: return@executeAction
            val rawData = ByteArray(info.size)
            outputBuffer.get(rawData)
            processRawData(decoder?.hashCode().toString(), rawData, info.presentationTimeUs)
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

    private fun executeAction(action: ()-> Unit) {
        try {
            action.invoke()
        } catch (e: Exception) {
            Log.d("decoder", "catch Exception:", e)
            throw e
        }
    }

    private external fun processRawData(decoderId: String, byteBuffer: ByteArray, presentationTimeUs: Long)

    companion object {
        private val decoders = HashMap<String, MediaCodecDecoder>()

        @JvmStatic @Synchronized
        fun createVideoDecoder(mediaInfo: String, width: Int, height: Int): String {
            val format = MediaFormat.createVideoFormat(mediaInfo, width, height)
            val extractorMaxSize = format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE, -1)
            if (extractorMaxSize > 0) {
                format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, extractorMaxSize)
            } else {
                format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 1048576 * 2) //2MB
            }
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
        fun sendData(decoderId: String, byteBuffer: ByteArray, pts: Long, flag: Int) {
            if (!decoders.contains(decoderId)) {
                return
            }
            decoders[decoderId]?.receivePacket(byteBuffer, pts, flag)
        }

        @JvmStatic @Synchronized
        fun doAction(decoderId: String, action: Action) {
            when (action) {
                Action.FLUSH -> decoders[decoderId]?.flush()
            }
        }
    }


}