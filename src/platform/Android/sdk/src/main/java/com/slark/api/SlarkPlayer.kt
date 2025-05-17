package com.slark.api

import android.view.Surface

/**
 * SlarkPlayer interface that defines the methods for a media player.
 */

interface SlarkPlayer {
    fun setConfig(config: SlarkPlayerConfig): Boolean

    fun getPlayerId(): String

    fun play()

    fun pause()

    fun stop()

    fun seekTo(time: Double)

    fun release()

    fun setObserver(observer: SlarkPlayerObserver)

    fun setRenderTarget(renderTarget: SlarkRenderTarget)
}