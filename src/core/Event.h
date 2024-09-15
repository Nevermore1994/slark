//
// Created by Nevermore on 2022/5/30.
// slark FlowEvent
// Copyright (c) 2022 Nevermore All rights reserved.
//
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace slark {

enum class Event: int8_t {
    Unknown = 0,
    Play,
    Pause,
    Stop,
    ReadCompleted,
    ReadError,
    DemuxCompleted,
    DemuxError,
    DecodeError,
    RenderFrameCompleted,
    RenderError,
    SwitchAudioDecoder,
    SwitchVideoDecoder,
    UpdateRenderSize,
};

enum class PlayerState : int8_t;

Event buildEvent(PlayerState state);
std::string_view EventTypeToString(Event type);
}

