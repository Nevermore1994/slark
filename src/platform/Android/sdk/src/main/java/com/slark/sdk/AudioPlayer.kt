package com.slark.sdk

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock

enum class DataFlag {
    Normal,
    EndOfStream,
    Silence,
    Error,
}

class AudioPlayer(private val sampleRate: Int, private val channelCount: Int) {
    private var audioTrack: AudioTrack? = null
    private var bufferSize = 0
    private val lock = ReentrantLock()
    private var lastRawPosition: Int = 0
    private var wrapCount: Long = 0
    private var isCompleted = false
    private val positionUpdateListener = object : AudioTrack.OnPlaybackPositionUpdateListener {
        override fun onMarkerReached(track: AudioTrack) {
        }

        override fun onPeriodicNotification(track: AudioTrack) {
            val available = getAvailableSpace()
            if (available == 0) {
                return
            }
            if (isCompleted) {
                write(ByteArray(available){0}) //write silence data
                return
            }
            val buffer = ByteBuffer.allocateDirect(available)
            buffer.order(ByteOrder.nativeOrder())
            val result = requestAudioData(hashCode().toString(), buffer)
            when (result) {
                DataFlag.Normal -> { write(buffer.array()) }
                DataFlag.EndOfStream -> isCompleted = true
                DataFlag.Silence -> write(ByteArray(available){0})
                DataFlag.Error -> {}
            }
        }
    }

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
        audioTrack?.setPositionNotificationPeriod((PULL_DATA_PERIOD / 1000.0 * sampleRate).toInt())
        audioTrack?.setPlaybackPositionUpdateListener(positionUpdateListener)
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
        audioTrack?.pause()
    }

    fun play() {
        audioTrack?.play()
    }

    fun release() {
        lock.withLock {
            try {
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

    fun getPlaybackPositionInMs(): Long {
        return audioTrack?.let { track ->
            val frames = getAbsolutePosition(track.playbackHeadPosition)
            (frames * 1000L) / sampleRate
        } ?: 0L
    }

    external fun requestAudioData(playerId: String, requestBuffer: ByteBuffer): DataFlag

    companion object {
        init {
            System.loadLibrary("sdk")
        }

        const val PULL_DATA_PERIOD = 10 //ms
        const val BUFFER_TIME_SIZE = 100 //ms
        const val LOG_TAG = "AudioPlayer"
        private val players = ConcurrentHashMap<String, AudioPlayer>()

        @JvmStatic
        fun createAudioPlayer(sampleRate: Int, channelCount: Int): String {
            val player = AudioPlayer(sampleRate, channelCount)
            val playerId = player.hashCode().toString()
            players[playerId] = player
            return playerId
        }

        @JvmStatic
        fun getPlayedTime(playerId: String): Long {
            if (!players.containsKey(playerId)) {
                SlarkLog.e(LOG_TAG,"not found player")
                return 0
            }
            return players[playerId]?.getPlaybackPositionInMs() ?: 0L
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
            if (!players.contains(playerId)) {
                return
            }
            when (config) {
                Config.Rate -> players[playerId]?.setPlayRate(value)
                Config.Volume -> players[playerId]?.setVolume(value)
            }
        }

    }
}