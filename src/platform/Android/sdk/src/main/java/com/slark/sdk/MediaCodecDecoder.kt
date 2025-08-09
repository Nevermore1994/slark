package com.slark.sdk

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.opengl.EGL14
import android.util.Size
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentHashMap

enum class DecodeMode(val value: Int) {
    Texture(0),
    ByteBuffer(1);

    companion object {
        fun fromInt(value: Int): DecodeMode {
            return entries.firstOrNull { it.value == value } ?: Texture
        }
    }
}

class MediaCodecDecoder(
    private val mediaInfo: String,
    private val format: MediaFormat,
    private val decodeMode: DecodeMode
) {
    var decoderId: String = hashCode().toString()
        private set
    private var decoder: MediaCodec? = null
    private var isRunning: Boolean = false
    private var surface: RenderSurface? = null
    private var isCompleted: Boolean = false

    enum class Action {
        Flush,
    }

    enum class ErrorCode(i: Int) {
        Unknown(0),
        Again(1),
        Success(2),
        NotProcessed(-1),
        NotStart(-2),
        InputDataError(-3),
        InputDataTooLarge(-4),
        NotFoundDecoder(-5),
        ErrorDecoder(-6),
    }

    init {
        decoder = MediaCodecDecoderPool.acquireDecoder(mediaInfo)
        decoder?.let { decoder ->
            decoder.reset()
            if (format.isVideoFormat()) {
                if (decodeMode == DecodeMode.Texture) {
                    surface = RenderSurface()
                    surface?.init()
                    decoder.configure(format, surface?.surface(), null, 0)
                } else {
                    // Use flexible color format for YUV420
                    format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible)
                    decoder.configure(format, null, null, 0)
                }
            } else {
                decoder.configure(format, null, null, 0)
            }
            decoder.start()
            isRunning = true
        } ?: run {
            SlarkLog.e(LOG_TAG, "create $mediaInfo decoder error")
            isRunning = false
        }
    }

    /// BUFFER_FLAG_CODEC_CONFIG: 2
    /// BUFFER_FLAG_DECODE_ONLY: 32
    /// BUFFER_FLAG_END_OF_STREAM: 4
    /// BUFFER_FLAG_KEY_FRAME: 1
    /// BUFFER_FLAG_PARTIAL_FRAME: 8
    fun sendPacket(byteBuffer: ByteArray?, presentationTimeUs: Long, flag: Int): Int {
        val mediaInfo =  if (format.isVideoFormat()) "video " else "audio "
        var errorCode: ErrorCode = ErrorCode.Unknown
        if (!isRunning) {
            errorCode = ErrorCode.NotStart
            return errorCode.ordinal
        }
        if (isCompleted) {
            errorCode = ErrorCode.InputDataError
            SlarkLog.e(LOG_TAG, "$mediaInfo coder is completed")
            return errorCode.ordinal
        }

        execute {
            val inputBufferIndex =
                decoder?.dequeueInputBuffer(10_000) ?: return@execute //10 ms
            if (inputBufferIndex == -1) {
                SlarkLog.e(LOG_TAG,  mediaInfo + "no input buffer available")
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
                val size = byteBuffer.size
                if (inputBuffer.capacity() < byteBuffer.size) {
                    SlarkLog.e(LOG_TAG, mediaInfo + "input buffer too small: ${inputBuffer.capacity()} < ${byteBuffer.size}")
                    errorCode = ErrorCode.InputDataTooLarge
                    return@execute
                }
                inputBuffer.put(byteBuffer)
                decoder?.queueInputBuffer(inputBufferIndex, 0, byteBuffer.size, presentationTimeUs, 0)
                SlarkLog.i(LOG_TAG, mediaInfo + "queued input buffer: $inputBufferIndex, size: $size, time: $presentationTimeUs")
            }
        }

        execute {
            val info = MediaCodec.BufferInfo()
            val outputBufferIndex =
                decoder?.dequeueOutputBuffer(info, 10_000) ?: return@execute //10 ms
            isCompleted = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
            @Suppress("DEPRECATION")
            when (outputBufferIndex) {
                MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                    val info = decoder?.outputFormat.toString()
                    SlarkLog.i(LOG_TAG, mediaInfo + "output format changed: $info")
                    val newFormat = decoder?.outputFormat
                    errorCode = ErrorCode.Again
                    return@execute
                }
                MediaCodec.INFO_TRY_AGAIN_LATER -> {
                    SlarkLog.i(LOG_TAG, mediaInfo + "no output buffer available")
                    errorCode = ErrorCode.Again
                    return@execute
                }
                MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED -> {
                    SlarkLog.i(LOG_TAG, mediaInfo + "output buffers changed")
                    @Suppress("DEPRECATION")
                    errorCode = ErrorCode.Again
                    return@execute
                }
                else -> {
                    if (format.isVideoFormat() && decodeMode == DecodeMode.Texture) {
                        SlarkLog.i(LOG_TAG, "decoded video pts: ${info.presentationTimeUs}")
                        decoder?.releaseOutputBuffer(outputBufferIndex, true)
                        errorCode = ErrorCode.Success
                    } else {
                        val outputBuffer =
                            decoder?.getOutputBuffer(outputBufferIndex) ?: return@execute
                        outputBuffer.position(info.offset)
                        outputBuffer.limit(info.size + info.offset)
                        val decodeData = extractBytes(outputBuffer)
                        outputBuffer.clear()
                        if (isCompleted) {
                            SlarkLog.i(LOG_TAG, "$mediaInfo decode completed")
                        }
                        processRawData(decoderId, decodeData, info.presentationTimeUs, isCompleted)
                        decoder?.releaseOutputBuffer(outputBufferIndex, false)
                        errorCode = ErrorCode.Success
                    }
                    return@execute
                }
            }
        }
        return errorCode.ordinal
    }

    fun release() {
        isRunning = false
        flush()
        decoder?.let { tDecoder ->
            MediaCodecDecoderPool.releaseDecoder(mediaInfo, tDecoder)
        }
        decoder = null
    }

    fun flush() {
        decoder?.flush()
        isCompleted = false
        val mediaInfo =  if (format.isVideoFormat()) "video " else "audio "
        SlarkLog.i(LOG_TAG, "$mediaInfo flush decoder")
    }

    fun isValid(): Boolean {
        return decoder != null
    }

    private fun execute(action: () -> Unit) {
        try {
            action.invoke()
        } catch (e: Exception) {
            SlarkLog.e(LOG_TAG, "catch Exception:{}", e.message)
            throw e
        }
    }

    fun requestVideoFrame(waitTime: Long, width: Int, height: Int): Long {
        clearGLStatus()
        val context = EGL14.eglGetCurrentContext()
        if (context == EGL14.EGL_NO_CONTEXT) {
            SlarkLog.e(LOG_TAG, "EGL context is not valid")
            return 0L
        }
        surface?.let {
            val res = it.awaitFrame(waitTime)
            if (res.first) {
                it.drawFrame(format, Size(width, height))
                // set high bit to indicate frame available
                val flag = (1L shl 63) or ((if (isCompleted) 1L else 0L) shl 62)
                return flag or (res.second and 0x3FFFFFFFFFFFFFFF)
            }
        }
        return (0L shl 63) or ((if (isCompleted) 1L else 0L) shl 62)
    }

    private external fun processRawData(decoderId: String, byteBuffer: ByteArray, presentationTimeUs: Long, isCompleted: Boolean)

    companion object {
        const val LOG_TAG = "MediaCodec"
        private val decoders = ConcurrentHashMap<String, MediaCodecDecoder>()

        fun createVideoFormat(mediaInfo: String, videoInfos: IntArray): MediaFormat {
            if (videoInfos.size < 4) {
                throw IllegalArgumentException("Invalid video info format")
            }
            return MediaFormat.createVideoFormat(mediaInfo, videoInfos[0].toInt(), videoInfos[1].toInt()).apply {
                setInteger(MediaFormat.KEY_PROFILE,  videoInfos[2].toInt())
                setInteger(MediaFormat.KEY_LEVEL,  videoInfos[3].toInt())
            }
        }

        @JvmStatic
        fun createVideoDecoder(mediaInfo: String, videoInfos: IntArray): String {
            val format = createVideoFormat(mediaInfo, videoInfos)
            val extractorMaxSize = format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE, -1)
            if (extractorMaxSize > 0) {
                format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, extractorMaxSize)
            } else {
                format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 1048576 * 2) //2MB
            }
            val decoder = MediaCodecDecoder(mediaInfo, format, DecodeMode.fromInt(if (videoInfos.size >=5) videoInfos[4] else 0 ))
            val decoderId = decoder.decoderId
            decoders[decoderId] = decoder
            return decoderId
        }

        @JvmStatic
        fun createAudioDecoder(mediaInfo: String,
                               audioInfos: IntArray): String {
            if (audioInfos.size < 6) {
                SlarkLog.e(LOG_TAG, "input param error!");
                return ""
            }
            val format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, audioInfos[0], audioInfos[1])
            val channelCount = audioInfos[1]
            val audioProfile = audioInfos[2]
            format.setInteger(MediaFormat.KEY_AAC_PROFILE, audioProfile)
            val extractorMaxSize = format.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE, -1)
            if (extractorMaxSize > 0) {
                format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, extractorMaxSize)
            } else {
                format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 4096)
            }

            val samplingFreqIndex = audioInfos[3]
            val extAudioType = audioInfos[4]
            val extSamplingFreqIndex = audioInfos[5]
            val config = ByteArray(4)
            if (audioProfile != MediaCodecInfo.CodecProfileLevel.AACObjectHE &&
                audioProfile != MediaCodecInfo.CodecProfileLevel.AACObjectHE_PS) {
                config[0] = ((audioProfile shl 3) or (samplingFreqIndex shr 1)).toByte()
                config[1] = (((samplingFreqIndex and 0x01) shl 7) or (channelCount shl 3)).toByte()
            } else {
                config[0] = ((audioProfile shl 3) or (samplingFreqIndex shr 1)).toByte()
                config[1] = (((samplingFreqIndex and 0x01) shl 7) or (channelCount shl 3) or (extSamplingFreqIndex shr 1)).toByte()
                config[2] = (((extSamplingFreqIndex and 0x01) shl 7) or (extAudioType shl 2)).toByte()
            }
            val csd0 = ByteBuffer.wrap(config)
            format.setByteBuffer("csd-0", csd0)

            val decoder = MediaCodecDecoder(MediaFormat.MIMETYPE_AUDIO_AAC, format, DecodeMode.ByteBuffer)
            val decoderId = decoder.decoderId
            decoders[decoderId] = decoder
            SlarkLog.i(LOG_TAG, "create audio: $decoderId")
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
    }

}