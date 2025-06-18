package com.slark.api

import android.util.Size
import com.slark.sdk.SlarkPlayerImpl

/**
 * SlarkPlayer interface that defines the methods for a media player.
 */

interface SlarkPlayer {
    var isLoop: Boolean

    var isMute: Boolean

    var volume: Float

    fun prepare()

    fun getPlayerId(): String

    fun play()

    fun pause()

    fun stop()

    /**
     * @param time: seconds
     * @param isAccurate:  Accurate seek is only performed when the user releases the progress slider.
     */
    fun seekTo(time: Double, isAccurate: Boolean)

    fun release()

    fun setObserver(observer: SlarkPlayerObserver?)

    fun setRenderTarget(renderTarget: SlarkRenderTarget)

    fun totalDuration(): Double

    fun currentTime(): Double

    fun setRenderSize(size: Size)
}

object SlarkPlayerFactory {
    fun createPlayer(config: SlarkPlayerConfig): SlarkPlayer? {
        val player = SlarkPlayerImpl.create(config)
        return player
    }
}