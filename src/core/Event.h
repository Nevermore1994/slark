//
// Created by Nevermore on 2022/5/30.
// slark FlowEvent
// Copyright (c) 2022 Nevermore All rights reserved.
//
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <any>

namespace slark {

enum class EventType: int8_t {
    Unknown = 0,
    Play,
    Pause,
    Stop,
    Seek,
    ReadCompleted,
    DemuxCompleted,
    DecodeCompleted,
    RenderFrameCompleted,
    ReadError,
    DemuxError,
    DecodeError,
    RenderError,
    UpdateSetting,
    UpdateSettingVolume,
    UpdateSettingMute,
    UpdateSettingRenderSize,
    UpdateSettingEnd,
};

enum class PlayerState : int8_t;

struct Event {
    EventType type = EventType::Unknown;
    std::any data;
    Event() = default;
    Event(EventType t)
        : type(t) {
        
    }
};
using EventPtr = std::unique_ptr<Event>;

EventPtr buildEvent(PlayerState state);
EventPtr buildEvent(EventType type);
std::string_view EventTypeToString(EventType type);
}

