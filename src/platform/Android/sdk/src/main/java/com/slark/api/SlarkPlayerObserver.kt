package com.slark.api

interface SlarkPlayerObserver {
    fun notifyTime(playerId: String, time: Double)

    fun notifyState(playerId: String, state: SlarkPlayerState)

    fun notifyEvent(playerId: String, event: SlarkPlayerEvent, value: String)
}