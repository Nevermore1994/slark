
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.api

interface SlarkPlayerObserver {
    fun notifyTime(playerId: String, time: Double)

    fun notifyState(playerId: String, state: SlarkPlayerState)

    fun notifyEvent(playerId: String, event: SlarkPlayerEvent, value: String)
}
