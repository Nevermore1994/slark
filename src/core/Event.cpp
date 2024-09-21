//
// Created by Nevermore
// slark Event
// Copyright (c) 2024 Nevermore All rights reserved.
//

#include "Event.h"
#include "Player.h"

namespace slark {

EventPtr buildEvent(PlayerState state) {
    EventType eventType = EventType::Unknown;
    switch (state) {
        case PlayerState::Playing: {
            eventType = EventType::Play;
        }
            break;
        case PlayerState::Pause: {
            eventType = EventType::Pause;
        }
            break;
        case PlayerState::Stop: {
            eventType = EventType::Stop;
        }
            break;
        default:
            break;
    }
    return std::make_unique<Event>(eventType);
}

EventPtr buildEvent(EventType type) {
    return std::make_unique<Event>(type);
}

#define ENUM_TO_STRING_CASE(value) case value: return std::string_view(#value)

std::string_view EventTypeToString(EventType type) {
    switch (type) {
        ENUM_TO_STRING_CASE(EventType::Play);
        ENUM_TO_STRING_CASE(EventType::Pause);
        ENUM_TO_STRING_CASE(EventType::Stop);
        ENUM_TO_STRING_CASE(EventType::ReadCompleted);
        ENUM_TO_STRING_CASE(EventType::ReadError);
        ENUM_TO_STRING_CASE(EventType::DemuxCompleted);
        ENUM_TO_STRING_CASE(EventType::DemuxError);
        ENUM_TO_STRING_CASE(EventType::DecodeError);
        ENUM_TO_STRING_CASE(EventType::RenderFrameCompleted);
        ENUM_TO_STRING_CASE(EventType::RenderError);
        default:
            return "Unknown";
    }
}

} // end of namespace slark

