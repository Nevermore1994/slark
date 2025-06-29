package com.slark.api

enum class SlarkPlayerEvent {
    FirstFrameRendered,
    SeekDone,
    PlayEnd,
    UpdateCacheTime,
    OnError,
}

///There is currently no specific error code
enum class PlayerErrorCode {
    OpenFileError,
    NetWorkError,
    NotSupport,
    DemuxError,
    DecodeError,
    RenderError;

    companion object {
        fun fromString(value: String): PlayerErrorCode? {
            return when (value.toInt()) {
                0 -> OpenFileError
                1 -> NetWorkError
                2 -> NotSupport
                3 -> DemuxError 
                4 -> DecodeError
                5 -> RenderError
                else -> null
            }
        }
    }
};