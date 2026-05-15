
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//


package com.slark.api

data class SlarkPlayerConfig(
    val dataSource: String, // URL or file path
    val timeRange: KtTimeRange = KtTimeRange.zero // Time range for playback
)
