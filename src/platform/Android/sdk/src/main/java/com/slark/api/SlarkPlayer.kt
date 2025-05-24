package com.slark.api

import com.slark.sdk.SlarkPlayerImpl

/**
 * SlarkPlayer interface that defines the methods for a media player.
 */

interface SlarkPlayer {

    fun getPlayerId(): String

    fun play()

    fun pause()

    fun stop()

    fun seekTo(time: Double)

    fun release()

    fun setObserver(observer: SlarkPlayerObserver?)

    fun setRenderTarget(renderTarget: SlarkRenderTarget)
}

object SlarkPlayerFactory {
    fun createPlayer(config: SlarkPlayerConfig): SlarkPlayer? {
        val player = SlarkPlayerImpl()
        return player.create(config)
    }
}