package com.slark.sdk

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.DelicateCoroutinesApi
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.newSingleThreadContext
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

enum class DataFlag {
    Normal,
    EndOfStream,
    Silence,
    Error;

    companion object {
        fun fromInt(value: Int): DataFlag {
            return when (value) {
                0 -> Normal
                1 -> EndOfStream
                2 -> Silence
                else -> Error
            }
        }
    }
}

class AudioPlayer(private val sampleRate: Int, private val channelCount: Int) {
    var playerId: String = hashCode().toString()
        private set
    private var audioTrack: AudioTrack? = null
    private var bufferSize = AudioTrack.getMinBufferSize(
        sampleRate,
        getChannelConfig(channelCount),
        AudioFormat.ENCODING_PCM_16BIT
    )
    private val lock = ReentrantLock()
    private var lastRawPosition: Int = 0
    private var wrapCount: Long = 0
    private var isCompleted = false
    private var audioJob: Job? = null
    private var writeSize: Long = 0
    private val bytesPerFrame = channelCount * 2 //16 bit pcm
    @OptIn(DelicateCoroutinesApi::class)
    private val audioDispatcher = newSingleThreadContext("AudioDataThread")
    private val audioAttribute = AudioAttributes.Builder()
        .setUsage(AudioAttributes.USAGE_MEDIA)
        .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
        .build()
    private val audioFormat = AudioFormat.Builder()
        .setSampleRate(sampleRate)
        .setChannelMask(getChannelConfig(channelCount))
        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
        .build()

    init {
        try {
            initAudioTrack()
        } catch (e: Exception) {
            SlarkLog.e(LOG_TAG, "Failed to initialize AudioTrack: ${e.message}")
            throw RuntimeException("Audio initialization failed", e)
        }
    }

    private fun initAudioTrack() {
        val minBufferSize = ((BUFFER_TIME_SIZE / 1000.0) * sampleRate * 2 * channelCount).toInt()
        if (bufferSize < minBufferSize) {
            bufferSize = minBufferSize
        }
        CoroutineScope(audioDispatcher).launch{
            lock.withLock {
                audioTrack = AudioTrack.Builder()
                    .setAudioAttributes(audioAttribute)
                    .setAudioFormat(audioFormat)
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .setBufferSizeInBytes(bufferSize)
                    .build()
            }
        }
    }

    private fun requestDataSize(): Int {
        return (PULL_DATA_PERIOD / 1000.0 * sampleRate).toInt()
    }

    enum class Action {
        Play,
        Pause,
        Flush,
        Release
    }

    enum class Config {
        Rate,
        Volume,
    }

    fun write(data: ByteArray): Int {
        lock.withLock {
            return audioTrack?.let { track ->
                try {
                    track.write(data, 0, data.size, AudioTrack.WRITE_BLOCKING)
                } catch (e: IllegalStateException) {
                    SlarkLog.e(LOG_TAG,"AudioTrack write failed: ${e.message}")
                    AudioTrack.ERROR
                }
            } ?: AudioTrack.ERROR
        }
    }

    fun pause() {
        SlarkLog.i(LOG_TAG, "pause")
        audioJob?.cancel()
        audioJob = null
        lock.withLock {
            audioTrack?.let { track ->
                if (track.playState == AudioTrack.PLAYSTATE_PAUSED ||
                    track.playState == AudioTrack.PLAYSTATE_STOPPED ) {
                    return
                }
                track.pause()
            }
        }
    }

    fun play() {
        SlarkLog.i(LOG_TAG, "play")
        lock.withLock {
            audioTrack?.let { track->
                if (track.playState == AudioTrack.PLAYSTATE_PLAYING) {
                    return
                }
                track.play()
            }

        }
        audioJob = CoroutineScope(audioDispatcher).launch {
            while (!isCompleted && isActive) {
                val available = getAvailableSpace()
                if (available < requestDataSize()) {
                    SlarkLog.i(LOG_TAG, "audio track pos: ${audioTrack?.playbackHeadPosition}," +
                        " state: ${audioTrack?.playState}")
                    delay(25)
                    continue // Not enough space to write data
                }
                val buffer = ByteBuffer.allocateDirect(available)
                buffer.order(ByteOrder.nativeOrder())
                val result = requestAudioData(playerId, buffer)
                val flag = DataFlag.fromInt(result shr 24)
                var getSize = result and 0x00FFFFFF
                buffer.position(0)
                buffer.limit(getSize)
                when (flag) {
                    DataFlag.Normal -> {
                        val res = write(extractBytes(buffer))
                        if (res > 0) {
                            writeSize += res
                            SlarkLog.i(LOG_TAG, "write audio data:$res")
                        } else {
                            SlarkLog.i(LOG_TAG, "write audio error:$res")
                        }
                    }
                    DataFlag.EndOfStream -> {
                        isCompleted = true
                        SlarkLog.e(LOG_TAG, "render audio completed")
                    }
                    DataFlag.Silence -> {
                        write(ByteArray(available){0})
                    }
                    DataFlag.Error -> {
                        SlarkLog.e(LOG_TAG, "Error requesting audio data")
                        isCompleted = true
                    }
                }
                delay(10)
            }
        }
    }

    fun release() {
        lock.withLock {
            try {
                audioJob?.cancel()
                audioJob = null
                audioTrack?.stop()
                audioTrack?.flush()
                audioTrack?.release()
            } catch (e: Exception) {
                SlarkLog.e(LOG_TAG, "AudioTrack release failed: ${e.message}")
            } finally {
                audioTrack = null
            }
        }
    }

    fun flush() {
        release()
        initAudioTrack()
        lastRawPosition = 0
        wrapCount = 0
        writeSize = 0
    }

    fun setVolume(vol: Float) {
        lock.withLock {
            audioTrack?.setVolume(vol)
        }
    }

    fun setPlayRate(rate: Float) {
        lock.withLock {
            val params = audioTrack?.playbackParams
            if (params != null) {
                params.setSpeed(rate).setPitch(1.0f)
                audioTrack?.playbackParams = params
            }
        }
    }

    fun getAvailableSpace(): Int {
        var playedFrames = 0
        lock.withLock {
            playedFrames = audioTrack?.playbackHeadPosition ?: return@withLock 0
        }
        val playedBytes = playedFrames * bytesPerFrame
        val bufferedBytes = writeSize - playedBytes
        val available = bufferSize - bufferedBytes
        return available.coerceAtLeast(0).toInt()
    }

    private fun getAbsolutePosition(currentRawPosition: Int): Long {
        if (currentRawPosition < lastRawPosition) {
            wrapCount++
        }
        lastRawPosition = currentRawPosition
        return (wrapCount shl 32) + (currentRawPosition.toLong() and 0xFFFFFFFFL)
    }

    fun getPlaybackPositionInUs(): Long {
        lock.withLock {
            return audioTrack?.let { track ->
                val frames = getAbsolutePosition(track.playbackHeadPosition)
                (frames * 1000000L) / sampleRate
            } ?: 0L
        }
    }

    external fun requestAudioData(playerId: String, requestBuffer: ByteBuffer): Int

    companion object {
        const val PULL_DATA_PERIOD = 50 //ms
        const val BUFFER_TIME_SIZE = 100 //ms
        const val LOG_TAG = "AudioPlayer"
        private val players = ConcurrentHashMap<String, AudioPlayer>()

        @JvmStatic
        fun createAudioPlayer(sampleRate: Int, channelCount: Int): String {
            val player = AudioPlayer(sampleRate, channelCount)
            val playerId = player.playerId
            players[playerId] = player
            return playerId
        }

        @JvmStatic
        fun getPlayedTime(playerId: String): Long {
            if (!players.containsKey(playerId)) {
                SlarkLog.e(LOG_TAG,"not found player")
                return 0
            }
            return players[playerId]?.getPlaybackPositionInUs() ?: 0L
        }

        @JvmStatic
        fun audioPlayerAction(playerId: String, action : Action) {
            if (!players.containsKey(playerId)) {
                return
            }
            when (action) {
                Action.Play  -> players[playerId]?.play()
                Action.Pause -> players[playerId]?.pause()
                Action.Flush -> players[playerId]?.flush()
                Action.Release -> {
                    players[playerId]?.release()
                    players.remove(playerId)
                }
            }
        }

        @JvmStatic
        fun audioPlayerConfig(playerId: String, config: Config, value: Float) {
            if (!players.containsKey(playerId)) {
                return
            }
            when (config) {
                Config.Rate -> players[playerId]?.setPlayRate(value)
                Config.Volume -> players[playerId]?.setVolume(value)
            }
        }

    }
}