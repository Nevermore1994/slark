package com.slark.api

enum class SlarkPlayerState {
    Unknown,
    Initializing,
    Ready,
    Buffering,
    Playing,
    Pause,
    Stop,
    Error,
    Completed,
}