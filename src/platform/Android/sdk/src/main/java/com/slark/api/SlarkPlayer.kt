package com.slark.api

import android.util.Size
import com.slark.sdk.SlarkPlayerManager

/**
 * @param path A local file or a network link.
 * @param timeRange The range to be played. The default is to play the entire duration.
 */
class SlarkPlayer(path: String, timeRange: KtTimeRange = KtTimeRange.zero) {
    var observer:SlarkPlayerObserver? = null
    private lateinit var playerId: String

    var isLoop: Boolean = false
        set(value) {
            field = value
            SlarkPlayerManager.setLoop(playerId, value)
        }

    var isMute: Boolean = false
        set(value) {
            field = value
            SlarkPlayerManager.setMute(playerId, value)
        }

    var volume: Float = 100.0f
        set(value) {
            field = value
            SlarkPlayerManager.setVolume(playerId, value)
        }

    init {
        playerId = SlarkPlayerManager.createPlayer(path,
            timeRange.start.toSeconds(),
            timeRange.duration.toSeconds())
    }

    fun play() {
        SlarkPlayerManager.doAction(playerId, SlarkPlayerManager.Action.PLAY.ordinal)
    }

    fun pause() {
        SlarkPlayerManager.doAction(playerId, SlarkPlayerManager.Action.PAUSE.ordinal)
    }

    fun stop() {
        SlarkPlayerManager.doAction(playerId, SlarkPlayerManager.Action.STOP.ordinal)
    }

    fun release() {
        SlarkPlayerManager.doAction(playerId, SlarkPlayerManager.Action.RELEASE.ordinal)
    }

    fun seek(time: Double, isAccurate: Boolean = false) {
        SlarkPlayerManager.seek(playerId, time, isAccurate)
    }

    fun setRenderSize(size: Size) {
        SlarkPlayerManager.setRenderSize(playerId, size.width, size.height)
    }

    fun totalDuration(): Double {
        return SlarkPlayerManager.totalDuration(playerId)
    }

    fun currentTime(): Double {
        return SlarkPlayerManager.currentPlayedTime(playerId)
    }

    fun playerId(): String {
        return playerId
    }

    fun state(): SlarkPlayerState {
        return SlarkPlayerManager.state(playerId)
    }
}