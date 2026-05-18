//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.sdk

import android.media.MediaCodec
import android.media.MediaFormat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.LinkedBlockingQueue

object MediaCodecDecoderPool {
    private val poolSizePerType = mapOf(
        MediaFormat.MIMETYPE_VIDEO_AVC to 2,
        MediaFormat.MIMETYPE_VIDEO_HEVC to 2,
        MediaFormat.MIMETYPE_AUDIO_AAC to 2
    )
    private val decoderPools = ConcurrentHashMap<String, LinkedBlockingQueue<MediaCodec>>()
    private val lock = Any()

    fun acquireDecoder(type: String): MediaCodec? {
        val pool = decoderPools.getOrPut(type) { LinkedBlockingQueue() }
        synchronized(lock) {
            val decoder = pool.poll()
            if (decoder != null) return decoder
            return try {
                MediaCodec.createDecoderByType(type)
            } catch (e: Exception) {
                SlarkLog.e(LOG_TAG, "createDecoderByType failed for $type: ${e.message}")
                null
            }
        }
    }

    fun releaseDecoder(type: String, decoder: MediaCodec) {
        val max = poolSizePerType[type] ?: 1
        val pool = decoderPools.getOrPut(type) { LinkedBlockingQueue() }
        if (pool.size < max) {
            try {
                // 停止后放回池，下次 acquireDecoder 调用 reset() 重新配置
                decoder.stop()
                pool.offer(decoder)
            } catch (e: Exception) {
                // 状态异常时直接释放，不放回池
                SlarkLog.e(LOG_TAG, "stop decoder failed, releasing: ${e.message}")
                try { decoder.release() } catch (_: Exception) {}
            }
        } else {
            try { decoder.stop() } catch (_: Exception) {}
            try { decoder.release() } catch (_: Exception) {}
        }
    }

    fun warmUpAsync(scope: CoroutineScope = CoroutineScope(Job() + Dispatchers.IO)) {
        scope.launch {
            poolSizePerType.forEach { (type, max) ->
                val pool = decoderPools.getOrPut(type) { LinkedBlockingQueue() }
                repeat(max - pool.size) {
                    try {
                        val decoder = MediaCodec.createDecoderByType(type)
                        pool.offer(decoder)
                    } catch (e: Exception) {
                        SlarkLog.e(LOG_TAG, "warmUp failed for $type: ${e.message}")
                    }
                }
            }
        }
    }

    fun clear() {
        decoderPools.forEach { (_, pool) ->
            while (true) {
                val decoder = pool.poll() ?: break
                try { decoder.stop() } catch (_: Exception) {}
                try { decoder.release() } catch (_: Exception) {}
            }
        }
        decoderPools.clear()
    }

    private const val LOG_TAG = "MediaCodecDecoderPool"
}
