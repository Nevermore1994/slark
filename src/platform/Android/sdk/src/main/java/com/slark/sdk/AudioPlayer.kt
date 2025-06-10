package com.slark.sdk

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
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
    private var bufferSize = 0
    private val lock = ReentrantLock()
    private var lastRawPosition: Int = 0
    private var wrapCount: Long = 0
    private var isCompleted = false
    private var audioJob: Job? = null
    init {
        lock.withLock {
            try {
                initAudioTrack()
            } catch (e: Exception) {
                SlarkLog.e(LOG_TAG, "Failed to initialize AudioTrack: ${e.message}")
                throw RuntimeException("Audio initialization failed", e)
            }
        }
    }

    private fun initAudioTrack() {
        val channelConfig = when (channelCount) {
            1 -> AudioFormat.CHANNEL_OUT_MONO
            2 -> AudioFormat.CHANNEL_OUT_STEREO
            4 -> AudioFormat.CHANNEL_OUT_QUAD
            6 -> AudioFormat.CHANNEL_OUT_5POINT1
            else -> AudioFormat.CHANNEL_OUT_DEFAULT
        }

        bufferSize = AudioTrack.getMinBufferSize(
            sampleRate,
            channelConfig,
            AudioFormat.ENCODING_PCM_16BIT
        )

        val minBufferSize = ((BUFFER_TIME_SIZE / 1000.0) * sampleRate * 2 * channelCount).toInt()
        if (bufferSize < minBufferSize) {
            bufferSize = minBufferSize
        }

        audioTrack = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setSampleRate(sampleRate)
                    .setChannelMask(channelConfig)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .build()
            )
            .setTransferMode(AudioTrack.MODE_STREAM)
            .setBufferSizeInBytes(bufferSize)
            .build()
    }

    //100ms pull data period
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
        return audioTrack?.let { track ->
            try {
                track.write(data, 0, data.size, AudioTrack.WRITE_NON_BLOCKING)
            } catch (e: IllegalStateException) {
                SlarkLog.e(LOG_TAG,"AudioTrack write failed: ${e.message}")
                AudioTrack.ERROR
            }
        } ?: AudioTrack.ERROR
    }

    fun pause() {
        if (audioTrack?.playState == AudioTrack.PLAYSTATE_PAUSED ||
            audioTrack?.playState == AudioTrack.PLAYSTATE_STOPPED ) {
            return
        }
        audioJob?.cancel()
        audioJob = null
        audioTrack?.pause()
    }

    fun play() {
        if (audioTrack?.playState == AudioTrack.PLAYSTATE_PLAYING) {
            return
        }
        audioTrack?.play()
        audioJob = CoroutineScope(Dispatchers.Default).launch {
            while (!isCompleted && isActive) {
                val available = getAvailableSpace()
                val requestSize = requestDataSize()
                if (available < requestDataSize()) {
                    delay(50)
                    continue // Not enough space to write data
                }
                val buffer = ByteBuffer.allocateDirect(requestSize)
                buffer.order(ByteOrder.nativeOrder())
                val result = requestAudioData(playerId, buffer)
                val flag = DataFlag.fromInt(result shr 24)
                var writeSize = result and 0x00FFFFFF
                buffer.position(0)
                buffer.limit(writeSize)
                when (flag) {
                    DataFlag.Normal -> {
                        write(extractBytes(buffer))
                    }
                    DataFlag.EndOfStream -> {
                        isCompleted = true
                    }
                    DataFlag.Silence -> {
                        write(ByteArray(available){0})
                    }
                    DataFlag.Error -> {
                        SlarkLog.e(LOG_TAG, "Error requesting audio data")
                        isCompleted = true
                    }
                }

                delay(20)
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
        audioTrack?.flush()
        lastRawPosition = 0
        wrapCount = 0
    }

    fun setVolume(vol: Float) {
        audioTrack?.setVolume(vol)
    }

    fun setPlayRate(rate: Float) {
        val params = audioTrack?.playbackParams
        if (params != null) {
            params.setSpeed(rate).setPitch(1.0f)
            audioTrack?.playbackParams = params
        }
    }

    fun getAvailableSpace(): Int {
        return audioTrack?.let { track ->
            val written = track.playbackHeadPosition
            bufferSize - (written % bufferSize)
        } ?: 0
    }

    private fun getAbsolutePosition(currentRawPosition: Int): Long {
        if (currentRawPosition < lastRawPosition) {
            wrapCount++
        }
        lastRawPosition = currentRawPosition
        return (wrapCount shl 32) + (currentRawPosition.toLong() and 0xFFFFFFFFL)
    }

    fun getPlaybackPositionInUs(): Long {
        return audioTrack?.let { track ->
            val frames = getAbsolutePosition(track.playbackHeadPosition)
            (frames * 1000000L) / sampleRate
        } ?: 0L
    }

    external fun requestAudioData(playerId: String, requestBuffer: ByteBuffer): Int

    companion object {
        const val PULL_DATA_PERIOD = 100 //ms
        const val BUFFER_TIME_SIZE = 200 //ms
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