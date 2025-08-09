package com.slark.api

enum class SlarkPlayerState {
    Unknown,
    Initializing,
    Prepared,
    Buffering,
    Ready,
    Playing,
    Pause,
    Stop,
    Error,
    Completed,
}