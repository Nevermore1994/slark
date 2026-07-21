#include "PlayerStateMachine.h"
#include <gtest/gtest.h>
#include <vector>
using namespace slark;

TEST(PlayerStateMachineFullTest, AllValidTransitions) {
    using enum PlayerState;
    std::vector<std::pair<PlayerState, PlayerState>> valid = {
        {NotInited, Initializing},
        {Initializing, Prepared}, {Initializing, Stop}, {Initializing, Error},
        {Prepared, Buffering}, {Prepared, Stop}, {Prepared, Error},
        {Buffering, Ready}, {Buffering, Playing}, {Buffering, Pause}, {Buffering, Stop}, {Buffering, Error},
        {Ready, Playing}, {Ready, Buffering}, {Ready, Stop}, {Ready, Error},
        {Playing, Pause}, {Playing, Buffering}, {Playing, Completed}, {Playing, Stop}, {Playing, Error},
        {Pause, Playing}, {Pause, Buffering}, {Pause, Stop}, {Pause, Error},
        {Completed, Playing}, {Completed, Pause}, {Completed, Buffering}, {Completed, Stop},
        {Error, Stop},
    };
    for (const auto& [from, to] : valid) {
        EXPECT_TRUE(PlayerStateMachine::isValidTransition(from, to))
            << "Valid " << static_cast<int>(from) << " -> " << static_cast<int>(to);
    }
}

TEST(PlayerStateMachineFullTest, AllInvalidTransitions) {
    using enum PlayerState;
    std::vector<std::pair<PlayerState, PlayerState>> invalid = {
        {NotInited, Prepared}, {NotInited, Playing}, {NotInited, Pause},
        {NotInited, Buffering}, {NotInited, Completed}, {NotInited, Ready},
        {NotInited, Stop}, {NotInited, Error},
        {Initializing, NotInited}, {Initializing, Playing}, {Initializing, Pause},
        {Initializing, Buffering}, {Initializing, Completed}, {Initializing, Ready},
        {Prepared, NotInited}, {Prepared, Initializing}, {Prepared, Playing},
        {Prepared, Pause}, {Prepared, Completed}, {Prepared, Ready},
        {Buffering, NotInited}, {Buffering, Initializing}, {Buffering, Prepared},
        {Buffering, Completed},
        {Ready, NotInited}, {Ready, Initializing}, {Ready, Prepared},
        {Ready, Pause}, {Ready, Completed},
        {Playing, NotInited}, {Playing, Initializing}, {Playing, Prepared},
        {Playing, Ready},
        {Pause, NotInited}, {Pause, Initializing}, {Pause, Prepared},
        {Pause, Completed}, {Pause, Ready},
        {Completed, NotInited}, {Completed, Initializing}, {Completed, Prepared},
        {Completed, Ready}, {Completed, Error},
        {Stop, NotInited}, {Stop, Initializing}, {Stop, Prepared},
        {Stop, Playing}, {Stop, Pause}, {Stop, Buffering},
        {Stop, Ready}, {Stop, Completed}, {Stop, Error},
        {Error, NotInited}, {Error, Initializing}, {Error, Prepared},
        {Error, Playing}, {Error, Pause}, {Error, Buffering},
        {Error, Ready}, {Error, Completed},
    };
    for (const auto& [from, to] : invalid) {
        EXPECT_FALSE(PlayerStateMachine::isValidTransition(from, to))
            << "Invalid " << static_cast<int>(from) << " -> " << static_cast<int>(to);
    }
}
