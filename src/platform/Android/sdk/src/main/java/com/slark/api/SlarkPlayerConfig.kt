
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//


package com.slark.api

data class SlarkPlayerConfig(
    val dataSource: String, // URL or file path
    val timeRange: KtTimeRange = KtTimeRange.zero // Time range for playback
)
