//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "PlayerStateMachine.h"

namespace slark {

bool PlayerStateMachine::isValidTransition(PlayerState from, PlayerState to) noexcept {
    using enum PlayerState;
    switch (from) {
        case NotInited:
            return to == Initializing;
        case Initializing:
            return to == Prepared || to == Stop || to == Error;
        case Prepared:
            return to == Buffering || to == Stop || to == Error;
        case Buffering:
            return to == Ready || to == Playing || to == Pause || to == Stop || to == Error;
        case Ready:
            return to == Playing || to == Buffering || to == Stop || to == Error;
        case Playing:
            return to == Pause || to == Buffering || to == Completed || to == Stop || to == Error;
        case Pause:
            return to == Playing || to == Buffering || to == Stop || to == Error;
        case Completed:
            return to == Playing || to == Pause || to == Buffering || to == Stop;
        case Stop:
            return false;
        case Error:
            return to == Stop;
        default:
            return false;
    }
}

std::expected<PlayerState, bool> PlayerStateMachine::getStateFromEvent(EventType event) noexcept {
    switch (event) {
        case EventType::Play:
            return PlayerState::Playing;
        case EventType::Pause:
            return PlayerState::Pause;
        case EventType::Stop:
            return PlayerState::Stop;
        case EventType::ReadError:
        case EventType::DecodeError:
        case EventType::DemuxError:
        case EventType::RenderError:
            return PlayerState::Error;
        case EventType::Unknown:
            return PlayerState::Unknown;
        default:
            return std::unexpected(false);
    }
}

std::expected<PlayerState, bool> PlayerStateMachine::getStateFromPipelineEvent(
    PipelineEvent event) noexcept {
    using enum PipelineEvent;
    switch (event) {
        case AudioDecoderFailed:
        case VideoDecoderFailed:
        case ReadError:
        case DecodeError:
            return PlayerState::Error;
        case BufferingStart:
            return PlayerState::Buffering;
        case RenderEnd:
            return PlayerState::Completed;
        default:
            return std::unexpected(false);
    }
}

} // namespace slark
