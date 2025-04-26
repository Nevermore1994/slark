package com.slark.sdk

import com.slark.api.SlarkPlayerState

class SlarkPlayerManager {
    enum class Action {
        PLAY,
        PAUSE,
        STOP,
        RELEASE
    }

    companion object {
        external fun createPlayer(path: String, start: Double, duration: Double): String

        external fun doAction(playerId: String, action: Int)

        external fun setLoop(playerId: String, isLoop: Boolean)

        external fun setVolume(playerId: String, volume: Float)

        external fun setMute(playerId: String, isMute: Boolean)

        external fun setRenderSize(playerId: String, width: Int, height: Int)

        external fun seek(playerId: String, time: Double, isAccurate: Boolean)

        external fun totalDuration(playerId: String):Double

        external fun currentPlayedTime(playerId: String): Double

        external fun state(playerId: String): SlarkPlayerState

    }
}