
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.sdk

data class DecodeFrameResult(
    val isAvailable: Boolean,
    val isCompleted: Boolean,
    val pts: Long
)
