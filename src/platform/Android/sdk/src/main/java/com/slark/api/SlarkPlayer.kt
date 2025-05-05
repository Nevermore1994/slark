package com.slark.api

import android.util.Size
import com.slark.sdk.AudioPlayer
import com.slark.sdk.SlarkPlayerManager
import java.util.concurrent.ConcurrentHashMap

/**
 * @param path A local file or a network link.
 * @param timeRange The range to be played. The default is to play the entire duration.
 */
class SlarkPlayer(path: String, timeRange: KtTimeRange = KtTimeRange.zero) {
    var observer:SlarkPlayerObserver? = null
    private var playerId: String = SlarkPlayerManager.createPlayer(path,
        timeRange.start.toSeconds(),
        timeRange.duration.toSeconds())

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
        SlarkPlayerManager.addPlayer(playerId, this)
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
        SlarkPlayerManager.removePlayer(playerId)
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

    fun setObserver(observer: SlarkPlayerObserver) {
        this.observer = observer
    }

}