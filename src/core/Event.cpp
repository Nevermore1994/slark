//
// Created by Nevermore
// slark Event
// Copyright (c) 2024 Nevermore All rights reserved.
//

#include "Event.h"
#include "Player.h"

namespace slark {

Event buildEvent(PlayerState state) {
    Event event = Event::Unknown;
    switch (state) {
        case PlayerState::Playing: {
            event = Event::Play;
        }
            break;
        case PlayerState::Pause: {
            event = Event::Pause;
        }
            break;
        case PlayerState::Stop: {
            event = Event::Stop;
        }
            break;
        default:
            break;
    }
    return event;
}

#define ENUM_TO_STRING_CASE(value) case value: return std::string_view(#value)

std::string_view EventTypeToString(Event type) {
    switch (type) {
        ENUM_TO_STRING_CASE(Event::Play);
        ENUM_TO_STRING_CASE(Event::Pause);
        ENUM_TO_STRING_CASE(Event::Stop);
        ENUM_TO_STRING_CASE(Event::ReadCompleted);
        ENUM_TO_STRING_CASE(Event::ReadError);
        ENUM_TO_STRING_CASE(Event::DemuxCompleted);
        ENUM_TO_STRING_CASE(Event::DemuxError);
        ENUM_TO_STRING_CASE(Event::DecodeError);
        ENUM_TO_STRING_CASE(Event::RenderFrameCompleted);
        ENUM_TO_STRING_CASE(Event::RenderError);
        default:
            return "Unknown";
    }
}

} // end of namespace slark

