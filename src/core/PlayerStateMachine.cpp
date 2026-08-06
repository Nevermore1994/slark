//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "PlayerStateMachine.h"

#include <array>
#include <cstddef>

namespace slark {

bool PlayerStateMachine::isValidTransition(PlayerState from, PlayerState to) noexcept {
    // [from][to] boolean transition table; row/column indices match the PlayerState enum values
    static constexpr std::array<std::array<bool, 11>, 11> kTransitions = {{
        /* NotInited   */ {
            {false, true, false, false, false, false, false, false, false, false, false}},
        /* Initializing */
        {{false, false, true, false, false, false, false, true, true, false, false}},
        /* Prepared    */
        {{false, false, false, true, false, false, false, true, true, false, false}},
        /* Buffering   */
        {{false, false, false, false, true, true, true, true, true, false, false}},
        /* Ready       */
        {{false, false, false, true, false, true, false, true, true, false, false}},
        /* Playing     */
        {{false, false, false, true, false, false, true, true, true, true, false}},
        /* Pause       */
        {{false, false, false, true, false, true, false, true, true, false, false}},
        /* Stop        */
        {{false, false, false, false, false, false, false, false, false, false, false}},
        /* Error       */
        {{false, false, false, false, false, false, false, true, false, false, false}},
        /* Completed   */
        {{false, false, false, true, false, true, true, true, false, false, false}},
        /* Unknown     */
        {{false, false, false, false, false, false, false, false, false, false, false}},
    }};
    const size_t fromIdx = static_cast<size_t>(from);
    const size_t toIdx = static_cast<size_t>(to);
    if (fromIdx >= kTransitions.size() || toIdx >= kTransitions[0].size()) {
        return false;
    }
    return kTransitions[fromIdx][toIdx];
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

}  // namespace slark
