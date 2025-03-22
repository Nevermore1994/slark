package com.slark.sdk

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.media.MediaCodecInfo
import android.media.PlaybackParams
import android.os.Build

class AudioPlayer(private val sampleRate: Int, private val channelCount: Int) {
    private var audioTrack: AudioTrack? = null
    private var bufferSize = 0

    init {
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


    fun write(data: ByteArray) {
        audioTrack?.write(data, 0, data.size, AudioTrack.WRITE_NON_BLOCKING)
    }

    fun pause() {
        audioTrack?.stop()
    }

    fun play() {
        audioTrack?.play()
    }

    fun release() {
        audioTrack?.release()
        audioTrack = null
    }

    fun flush() {
        audioTrack?.flush()
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

    enum class Action {
        PLAY,
        PAUSE,
        FLUSH,
        RELEASE
    }

    enum class Config {
        PLAY_RATE,
        PLAY_VOL,
    }

    companion object {
        init {
            System.loadLibrary("sdk")
        }

        const val BUFFER_TIME_MS = 10

        private val players = HashMap<String, AudioPlayer>()

        @JvmStatic @Synchronized
        fun createAudioPlayer(sampleRate: Int, channelCount: Int): String {
            val player = AudioPlayer(sampleRate, channelCount)
            val playerId = player.hashCode().toString()
            players[playerId] = player
            return playerId
        }

        @JvmStatic @Synchronized
        fun sendAudioData(playerId: String, data: ByteArray){
            if (!players.contains(playerId)) {
                return
            }
            players[playerId]?.write(data)
        }

        @JvmStatic @Synchronized
        fun audioPlayerAction(playerId: String, action : Action) {
            if (!players.contains(playerId)) {
                return
            }
            when (action) {
                Action.PLAY  -> players[playerId]?.play()
                Action.PAUSE -> players[playerId]?.pause()
                Action.FLUSH -> players[playerId]?.flush()
                Action.RELEASE -> {
                    players[playerId]?.release()
                    players.remove(playerId)
                }
            }
        }

        @JvmStatic @Synchronized
        fun audioPlayerConfig(playerId: String, config: Config, value: Float) {
            if (!players.contains(playerId)) {
                return
            }
            when (config) {
                Config.PLAY_RATE -> players[playerId]?.setVolume(value)
                Config.PLAY_VOL -> players[playerId]?.setPlayRate(value)
            }
        }

    }
}