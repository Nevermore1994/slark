//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.sdk

import com.slark.api.SlarkPlayerErrorCode
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.opengl.EGL14
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.util.Size
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CountDownLatch
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

class MediaCodecDecoder(
    private val mediaInfo: String,
    private val format: MediaFormat,
    private val decodeMode: DecodeMode,
    private val graphicsSurface: IGraphicsSurface? = null,
    private val graphicsContext: IGraphicsContext? = null
) {
    var decoderId: String = hashCode().toString()
        private set
    private var decoder: MediaCodec? = null
    private val isRunning = AtomicBoolean(false)
    private val isCompleted = AtomicBoolean(false)
    private var surface: IGraphicsSurface? = null

    private val inputBufferQueue = LinkedBlockingQueue<Int>()
    private val outputBufferQueue = LinkedBlockingQueue<Pair<Int, MediaCodec.BufferInfo>>()
    private val codecLock = Any()
    @Volatile
    private var codecGeneration = 0L

    private var callbackThread: HandlerThread? = null
    private var callbackHandler: Handler? = null

    enum class Action {
        Flush,
    }

    init {
        decoder = MediaCodecDecoderPool.acquireDecoder(mediaInfo)
        val dec = decoder
        if (dec != null) {
            dec.reset()
            configureDecoder(dec)
            val thread = HandlerThread("MediaCodec-cb-$decoderId").apply { start() }
            val handler = Handler(thread.looper)
            callbackThread = thread
            callbackHandler = handler
            dec.setCallback(makeCodecCallback(), handler)
            dec.start()
            isRunning.set(true)
        } else {
            SlarkLog.e(LOG_TAG, "create $mediaInfo decoder error")
        }
    }

    private fun configureDecoder(dec: MediaCodec) {
        if (format.isVideoFormat()) {
            if (decodeMode == DecodeMode.Texture) {
                surface = graphicsSurface
                    ?: graphicsContext?.createSurface()
                    ?: GLGraphicsSurface().also { it.init() }
                dec.configure(format, surface?.surface, null, 0)
            } else {
                format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible)
                dec.configure(format, null, null, 0)
            }
        } else {
            dec.configure(format, null, null, 0)
        }
    }

    private fun makeCodecCallback() = object : MediaCodec.Callback() {
        override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {
            val genAtEntry = codecGeneration
            synchronized(codecLock) {
                if (codecGeneration == genAtEntry) {
                    inputBufferQueue.offer(index)
                }
            }
        }

        override fun onOutputBufferAvailable(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
            if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) {
                isCompleted.set(true)
            }
            if (format.isVideoFormat() && decodeMode == DecodeMode.Texture) {
                val genAtEntry = codecGeneration
                synchronized(codecLock) {
                    if (codecGeneration == genAtEntry) {
                        outputBufferQueue.offer(index to info)
                        processRawData(decoderId, null, info.presentationTimeUs, isCompleted.get())
                    }
                }
            } else {
                consumeOutputBuffer(codec, index, info)
            }
        }

        override fun onError(codec: MediaCodec, e: MediaCodec.CodecException) {
            SlarkLog.e(LOG_TAG, "codec error: ${e.message}")
            isRunning.set(false)
            // Wake requestVideoFrame that may be blocked on outputBufferQueue
            outputBufferQueue.offer(-1 to MediaCodec.BufferInfo())
            processCodecError(decoderId, e.errorCode)
        }

        override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) {
            if (!format.isVideoFormat()) return
            val newWidth = format.getIntegerOrNull(MediaFormat.KEY_WIDTH) ?: return
            val newHeight = format.getIntegerOrNull(MediaFormat.KEY_HEIGHT) ?: return
            onVideoSizeChanged(decoderId, newWidth, newHeight)
        }
    }

    private fun consumeOutputBuffer(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
        val genAtEntry = codecGeneration
        var bufferValid = false
        try {
            val outputBuffer = codec.getOutputBuffer(index)
            bufferValid = true
            if (outputBuffer != null && info.size > 0) {
                outputBuffer.position(info.offset)
                outputBuffer.limit(info.size + info.offset)
                val decodeData = extractBytes(outputBuffer)
                outputBuffer.clear()
                processRawData(decoderId, decodeData, info.presentationTimeUs, isCompleted.get())
            } else if (isCompleted.get()) {
                // EOS with no data: still notify C++ so decoder knows it's done
                processRawData(decoderId, null, 0L, true)
            }
        } catch (e: Exception) {
            SlarkLog.e(LOG_TAG, "output buffer consume error: ${e.message}")
        } finally {
            if (bufferValid) {
                synchronized(codecLock) {
                    if (codecGeneration == genAtEntry) {
                        try {
                            codec.releaseOutputBuffer(index, false)
                        } catch (e: Exception) {
                            // Last-resort guard for the flush-vs-callback race: the buffer may
                            // have just been invalidated before release
                            SlarkLog.e(LOG_TAG, "releaseOutputBuffer error: ${e.message}")
                        }
                    }
                }
            }
        }
    }

    fun sendPacket(byteBuffer: ByteArray?, presentationTimeUs: Long, flag: Int): Int {
        if (!isRunning.get()) {
            return SlarkPlayerErrorCode.CodecNotStarted.value
        }
        if (isCompleted.get()) {
            SlarkLog.e(LOG_TAG, "${if (format.isVideoFormat()) "video" else "audio"} decoder is completed")
            return SlarkPlayerErrorCode.CodecInputError.value
        }

        val genAtEntry = codecGeneration
        val inputIndex = inputBufferQueue.poll()
            ?: return SlarkPlayerErrorCode.CodecNotProcessed.value

        synchronized(codecLock) {
            if (codecGeneration != genAtEntry || !isRunning.get()) {
                return SlarkPlayerErrorCode.CodecNotProcessed.value
            }
            try {
                val inputBuffer = decoder!!.getInputBuffer(inputIndex)!!
                inputBuffer.clear()
                if (flag.hasFlag(MediaCodec.BUFFER_FLAG_END_OF_STREAM)) {
                    decoder!!.queueInputBuffer(inputIndex, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                } else {
                    if (byteBuffer == null || byteBuffer.isEmpty()) {
                        return SlarkPlayerErrorCode.CodecInputError.value
                    }
                    if (inputBuffer.capacity() < byteBuffer.size) {
                        SlarkLog.e(LOG_TAG, "input buffer too small: ${inputBuffer.capacity()} < ${byteBuffer.size}")
                        return SlarkPlayerErrorCode.CodecInputTooLarge.value
                    }
                    inputBuffer.put(byteBuffer)
                    val queueFlag = if (flag.hasFlag(MediaCodec.BUFFER_FLAG_CODEC_CONFIG)) {
                        MediaCodec.BUFFER_FLAG_CODEC_CONFIG
                    } else {
                        0
                    }
                    decoder!!.queueInputBuffer(inputIndex, 0, byteBuffer.size, presentationTimeUs, queueFlag)
                }
            } catch (e: Exception) {
                SlarkLog.e(LOG_TAG, "queueInputBuffer exception: ${e.message}")
                return SlarkPlayerErrorCode.CodecNotProcessed.value
            }
        }
        return SlarkPlayerErrorCode.Success.value
    }

    fun release() {
        isRunning.set(false)
        outputBufferQueue.offer(-1 to MediaCodec.BufferInfo())
        flush()
        decoder?.let { MediaCodecDecoderPool.releaseDecoder(mediaInfo, it) }
        decoder = null
        callbackThread?.quitSafely()
        callbackThread = null
        callbackHandler = null
        surface?.release()
        surface = null
    }

    fun flush() {
        val handler = callbackHandler
        if (handler == null || Looper.myLooper() == handler.looper) {
            flushLocked()
            return
        }
        val latch = CountDownLatch(1)
        handler.post {
            try {
                flushLocked()
            } finally {
                latch.countDown()
            }
        }
        if (!latch.await(500, TimeUnit.MILLISECONDS)) {
            SlarkLog.e(LOG_TAG, "flush timeout waiting on callback thread")
        }
    }

    private fun flushLocked() {
        synchronized(codecLock) {
            decoder?.flush()
            drainNonBlocking(inputBufferQueue)
            drainNonBlocking(outputBufferQueue)
            isCompleted.set(false)
            decoder?.start()
            codecGeneration++
        }
    }

    private fun <T> drainNonBlocking(queue: LinkedBlockingQueue<T>) {
        val MAX_COUNT = 4096
        var count = 0
        while (queue.poll() != null && count < MAX_COUNT) {
            count++
        }
    }

    fun isValid(): Boolean = decoder != null

    fun requestVideoFrame(waitTime: Long, width: Int, height: Int): Long {
        clearGLStatus()
        val context = EGL14.eglGetCurrentContext()
        if (context == EGL14.EGL_NO_CONTEXT) {
            SlarkLog.e(LOG_TAG, "EGL context is not valid")
            return packFrameResult(false, isCompleted.get(), 0L)
        }
        val startNs = System.nanoTime()
        val output = outputBufferQueue.poll(waitTime, TimeUnit.MILLISECONDS)
            ?: return packFrameResult(false, isCompleted.get(), 0L)
        val (outputIndex, info) = output
        // Sentinel injected by onError, indicating the decoder failed
        if (outputIndex < 0) {
            return packFrameResult(false, isCompleted.get(), 0L)
        }
        val genAtEntry = codecGeneration
        var surfaceRef: IGraphicsSurface? = null
        synchronized(codecLock) {
            if (codecGeneration != genAtEntry) {
                return packFrameResult(false, isCompleted.get(), 0L)
            }
            val ref = surface
            try {
                if (ref == null) {
                    decoder?.releaseOutputBuffer(outputIndex, false)
                    return packFrameResult(false, isCompleted.get(), 0L)
                }
                decoder?.releaseOutputBuffer(outputIndex, true)
            } catch (e: Exception) {
                SlarkLog.e(LOG_TAG, "releaseOutputBuffer(render) error: ${e.message}")
                return packFrameResult(false, isCompleted.get(), 0L)
            }
            surfaceRef = ref
        }
        // Wait for the SurfaceTexture callback with the remaining time, keeping the total
        // wait within waitTime
        val elapsedMs = (System.nanoTime() - startNs) / 1_000_000L
        val remainMs = (waitTime - elapsedMs).coerceAtLeast(1L)
        val (frameReady, _) = surfaceRef!!.awaitFrame(remainMs)
        if (frameReady) {
            surfaceRef!!.drawFrame(format, Size(width, height))
            return packFrameResult(true, isCompleted.get(), info.presentationTimeUs)
        }
        return packFrameResult(false, isCompleted.get(), 0L)
    }
    private fun packFrameResult(isAvailable: Boolean, completed: Boolean, pts: Long): Long {
        val availBit = if (isAvailable) 1L shl 63 else 0L
        val completeBit = if (completed) 1L shl 62 else 0L
        return availBit or completeBit or (pts and 0x3FFFFFFFFFFFFFFF)
    }

    private external fun processRawData(decoderId: String, byteBuffer: ByteArray?, presentationTimeUs: Long, isCompleted: Boolean)
    private external fun processCodecError(decoderId: String, errorCode: Int)
    private external fun onVideoSizeChanged(decoderId: String, width: Int, height: Int)

    companion object {
        const val LOG_TAG = "MediaCodec"
        private val decoders = ConcurrentHashMap<String, MediaCodecDecoder>()

        @JvmStatic
        fun createVideoDecoder(mediaInfo: String, config: VideoDecoderConfig, csdBytes: ByteArray? = null): String {
            val format = MediaFormat.createVideoFormat(mediaInfo, config.width, config.height).apply {
                setInteger(MediaFormat.KEY_PROFILE, config.profile)
                setInteger(MediaFormat.KEY_LEVEL, config.level)
                setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 1048576 * 10)
                if (csdBytes != null) {
                    setByteBuffer("csd-0", java.nio.ByteBuffer.wrap(csdBytes))
                }
            }
            val decoder = MediaCodecDecoder(mediaInfo, format, config.decodeMode)
            if (!decoder.isValid()) {
                SlarkLog.e(LOG_TAG, "createVideoDecoder failed for $mediaInfo")
                return ""
            }
            val id = decoder.decoderId
            decoders[id] = decoder
            return id
        }

        @JvmStatic
        fun createAudioDecoder(mediaInfo: String, config: AudioDecoderConfig): String {
            val format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, config.sampleRate, config.channels).apply {
                setInteger(MediaFormat.KEY_AAC_PROFILE, config.profile)
                setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 4096)
            }
            val csdBytes = buildAacCsd(config)
            format.setByteBuffer("csd-0", ByteBuffer.wrap(csdBytes))

            val decoder = MediaCodecDecoder(MediaFormat.MIMETYPE_AUDIO_AAC, format, DecodeMode.ByteBuffer)
            if (!decoder.isValid()) {
                SlarkLog.e(LOG_TAG, "createAudioDecoder failed")
                return ""
            }
            val id = decoder.decoderId
            decoders[id] = decoder
            return id
        }

        private fun buildAacCsd(config: AudioDecoderConfig): ByteArray {
            val bytes = ByteArray(4)
            val isHE = config.profile == MediaCodecInfo.CodecProfileLevel.AACObjectHE ||
                config.profile == MediaCodecInfo.CodecProfileLevel.AACObjectHE_PS
            if (!isHE) {
                bytes[0] = ((config.profile shl 3) or (config.samplingFreqIndex shr 1)).toByte()
                bytes[1] = (((config.samplingFreqIndex and 0x01) shl 7) or (config.channels shl 3)).toByte()
            } else {
                bytes[0] = ((config.profile shl 3) or (config.samplingFreqIndex shr 1)).toByte()
                bytes[1] = (((config.samplingFreqIndex and 0x01) shl 7) or (config.channels shl 3) or (config.samplingFreqIndexExt shr 1)).toByte()
                bytes[2] = (((config.samplingFreqIndexExt and 0x01) shl 7) or (config.audioObjectTypeExt shl 2)).toByte()
            }
            return bytes
        }

        @JvmStatic
        fun sendPacket(decoderId: String, byteBuffer: ByteArray?, presentationTimeUs: Long, flag: Int): Int {
            val decoder = decoders[decoderId] ?: run {
                SlarkLog.e(LOG_TAG, "not found decoder: $decoderId")
                return SlarkPlayerErrorCode.CodecNotFound.value
            }
            return decoder.sendPacket(byteBuffer, presentationTimeUs, flag)
        }

        @JvmStatic
        fun requestVideoFrame(decoderId: String, waitTime: Long, width: Int, height: Int): Long {
            val decoder = decoders[decoderId] ?: run {
                SlarkLog.e(LOG_TAG, "not found decoder: $decoderId")
                return 0L
            }
            return decoder.requestVideoFrame(waitTime, width, height)
        }

        @JvmStatic
        fun releaseDecoder(decoderId: String) {
            decoders.remove(decoderId)?.release()
        }

        @JvmStatic @Synchronized
        fun doAction(decoderId: String, action: Action) {
            when (action) {
                Action.Flush -> decoders[decoderId]?.flush()
            }
        }
    }
}
