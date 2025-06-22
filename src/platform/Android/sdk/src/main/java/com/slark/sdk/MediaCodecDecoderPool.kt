package com.slark.sdk

import android.media.MediaCodec
import android.media.MediaFormat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.DelicateCoroutinesApi
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.LinkedBlockingQueue

object MediaCodecDecoderPool {
    private val poolSizePerType = mapOf(
        MediaFormat.MIMETYPE_VIDEO_AVC to 2, //H264
        //MediaFormat.MIMETYPE_VIDEO_HEVC to 2, // H265
        MediaFormat.MIMETYPE_AUDIO_AAC to 2 // AAC
    )
    private val decoderPools = ConcurrentHashMap<String, LinkedBlockingQueue<MediaCodec>>()
    private val lock = Any()

    fun acquireDecoder(
        type: String
    ): MediaCodec? {
        val pool = decoderPools.getOrPut(type) { LinkedBlockingQueue() }
        synchronized(lock) {
            var decoder = pool.poll()
            if (decoder != null) {
                return decoder
            }
            decoder = MediaCodec.createDecoderByType(type)
            return decoder
        }
    }

    fun releaseDecoder(type: String, decoder: MediaCodec) {
        val max = poolSizePerType[type]!!
        val pool = decoderPools.getOrPut(type) { LinkedBlockingQueue() }
        if (pool.size < max) {
            pool.offer(decoder)
        }
    }

    fun warmUp() {
        poolSizePerType.forEach { (type, max) ->
            val pool = decoderPools.getOrPut(type) { LinkedBlockingQueue() }
            val size = pool.size
            for (i in size until max) {
                val dummyFormat = MediaFormat()
                dummyFormat.setString(MediaFormat.KEY_MIME, type)
                val decoder = MediaCodec.createDecoderByType(type)
                pool.offer(decoder)
            }
        }
    }

    fun warmUpAsync(scope: CoroutineScope = CoroutineScope(Job() + Dispatchers.IO)) {
        scope.launch {
            poolSizePerType.forEach { (type, max) ->
                val pool = decoderPools.getOrPut(type) { LinkedBlockingQueue() }
                for (i in pool.size until max) {
                    val decoder = MediaCodec.createDecoderByType(type)
                    if (isVideoMime(type)) {
                        val dummyFormat = MediaFormat.createVideoFormat(type, 640, 360)
                        decoder.configure(dummyFormat, null, null, 0)
                    } else if (isAudioMime(type)) {
                        val dummyFormat = MediaFormat.createAudioFormat(type, 44100, 2)
                        decoder.configure(dummyFormat, null, null, 0)
                    }

                    pool.offer(decoder)
                }
            }
        }
    }

    fun clear() {
        decoderPools.forEach { (_, pool) ->
            while (true) {
                val decoder = pool.poll() ?: break
                decoder.release()
            }
        }
        decoderPools.clear()
    }

} 