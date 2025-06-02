package com.slark.api

enum class SlarkPlayerState {
    Unknown,
    Initializing,
    Prepared,
    Ready,
    Buffering,
    Playing,
    Pause,
    Stop,
    Error,
    Completed,
}