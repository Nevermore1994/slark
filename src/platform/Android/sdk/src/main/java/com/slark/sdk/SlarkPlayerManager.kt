
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//


package com.slark.sdk

import com.slark.api.SlarkPlayerEvent
import com.slark.api.SlarkPlayerState
import java.util.concurrent.ConcurrentHashMap

class SlarkPlayerManager {
    enum class Action {
        PREPARE,
        PLAY,
        PAUSE,
        RELEASE
    }

    companion object {
        private val players = ConcurrentHashMap<String, SlarkPlayerImpl>()

        @JvmStatic
        fun addPlayer(playerId: String, player: SlarkPlayerImpl) {
            players[playerId] = player
        }

        @JvmStatic
        fun removePlayer(playerId: String) {
            players.remove(playerId)
        }

        @JvmStatic
        fun notifyPlayerState(playerId: String, state: SlarkPlayerState) {
            players[playerId]?.observer()?.notifyState(playerId, state)
        }

        @JvmStatic
        fun notifyPlayerEvent(playerId: String, event: SlarkPlayerEvent, value: String) {
            players[playerId]?.observer()?.notifyEvent(playerId, event, value)
        }

        @JvmStatic
        fun notifyPlayerTime(playerId: String, time: Double) {
            players[playerId]?.observer()?.notifyTime(playerId, time)
        }

        @JvmStatic
        fun requestRender(playerId: String, textureId: Int, width: Int, height: Int) {
            players[playerId]?.requestRender(textureId, width, height)
        }

        // Symmetric to iOS: start CADisplayLink when Prepared
        @JvmStatic
        fun startRendering(playerId: String) {
            players[playerId]?.startRendering()
        }

        // Symmetric to iOS: stop CADisplayLink when Stop/Error
        @JvmStatic
        fun stopRendering(playerId: String) {
            players[playerId]?.stopRendering()
        }

        @JvmStatic
        fun setVideoRotation(playerId: String, rotationDegrees: Int) {
            players[playerId]?.setVideoRotation(rotationDegrees)
        }

        external fun createPlayer(path: String, start: Double, duration: Double): String

        external fun doAction(playerId: String, action: Int)

        external fun setLoop(playerId: String, isLoop: Boolean)

        external fun setVolume(playerId: String, volume: Float)

        external fun setMute(playerId: String, isMute: Boolean)

        external fun seek(playerId: String, time: Double, isAccurate: Boolean)

        external fun totalDuration(playerId: String): Double

        external fun currentPlayedTime(playerId: String): Double

        external fun state(playerId: String): SlarkPlayerState

        external fun hasVideo(playerId: String): Boolean

        external fun isSeekable(playerId: String): Boolean

    }
}
