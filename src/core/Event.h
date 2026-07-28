//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <any>
#include <cstdint>
#include <memory>
#include <string_view>

namespace slark {

enum class EventType : uint8_t {
    Unknown = 0,
    Play,
    Pause,
    Stop,
    Seek,
    ReadError,
    DemuxError,
    DecodeError,
    RenderError,
    UpdateSetting,
    UpdateSettingVolume,
    UpdateSettingMute,
    UpdateSettingEnd,
};

enum class PlayerState : uint8_t;

struct Event {
    EventType type = EventType::Unknown;
    std::any data;
    Event() = default;

    Event(EventType t) : type(t) {}
};

using EventPtr = std::unique_ptr<Event>;

EventPtr buildEvent(PlayerState state);
EventPtr buildEvent(EventType type);
std::string_view EventTypeToString(EventType type);
}  // namespace slark
