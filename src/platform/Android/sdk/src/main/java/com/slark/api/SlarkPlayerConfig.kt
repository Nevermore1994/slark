package com.slark.api

data class SlarkPlayerConfig(
    val dataSource: String, // URL or file path
    val timeRange: KtTimeRange // Time range for playback
)
