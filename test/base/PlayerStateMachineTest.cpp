//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "PlayerStateMachine.h"

#include "Event.h"

#include <gtest/gtest.h>

using namespace slark;

// MARK: - isValidTransition 测试

TEST(PlayerStateMachineTest, ValidTransitionFromNotInited) {
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::NotInited, PlayerState::Initializing));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::NotInited, PlayerState::Prepared));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::NotInited, PlayerState::Playing));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::NotInited, PlayerState::Stop));
}

TEST(PlayerStateMachineTest, ValidTransitionFromInitializing) {
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::Initializing, PlayerState::Prepared));
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::Initializing, PlayerState::Stop));
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::Initializing, PlayerState::Error));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::Initializing, PlayerState::Playing));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::Initializing, PlayerState::Buffering));
}

TEST(PlayerStateMachineTest, ValidTransitionFromPrepared) {
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::Prepared, PlayerState::Buffering));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Prepared, PlayerState::Stop));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Prepared, PlayerState::Error));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::Prepared, PlayerState::Playing));
}

TEST(PlayerStateMachineTest, ValidTransitionFromBuffering) {
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Buffering, PlayerState::Ready));
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::Buffering, PlayerState::Playing));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Buffering, PlayerState::Pause));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Buffering, PlayerState::Stop));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Buffering, PlayerState::Error));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::Buffering, PlayerState::Prepared));
}

TEST(PlayerStateMachineTest, ValidTransitionFromReady) {
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Ready, PlayerState::Playing));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Ready, PlayerState::Buffering));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Ready, PlayerState::Stop));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Ready, PlayerState::Error));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Ready, PlayerState::Prepared));
}

TEST(PlayerStateMachineTest, ValidTransitionFromPlaying) {
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Playing, PlayerState::Pause));
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::Playing, PlayerState::Buffering));
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::Playing, PlayerState::Completed));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Playing, PlayerState::Stop));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Playing, PlayerState::Error));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Playing, PlayerState::Ready));
}

TEST(PlayerStateMachineTest, ValidTransitionFromPause) {
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Pause, PlayerState::Playing));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Pause, PlayerState::Buffering));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Pause, PlayerState::Stop));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Pause, PlayerState::Error));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Pause, PlayerState::Ready));
}

TEST(PlayerStateMachineTest, ValidTransitionFromCompleted) {
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Completed, PlayerState::Stop));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::Completed, PlayerState::Playing));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Completed, PlayerState::Pause));
}

TEST(PlayerStateMachineTest, ValidTransitionFromStopAndError) {
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Stop, PlayerState::Playing));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::Stop, PlayerState::Initializing));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Error, PlayerState::Stop));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Error, PlayerState::Playing));
}

// MARK: - getStateFromEvent 测试

TEST(PlayerStateMachineTest, GetStateFromPlayPauseStopEvents) {
    auto playResult = PlayerStateMachine::getStateFromEvent(EventType::Play);
    ASSERT_TRUE(playResult.has_value());
    EXPECT_EQ(playResult.value(), PlayerState::Playing);

    auto pauseResult = PlayerStateMachine::getStateFromEvent(EventType::Pause);
    ASSERT_TRUE(pauseResult.has_value());
    EXPECT_EQ(pauseResult.value(), PlayerState::Pause);

    auto stopResult = PlayerStateMachine::getStateFromEvent(EventType::Stop);
    ASSERT_TRUE(stopResult.has_value());
    EXPECT_EQ(stopResult.value(), PlayerState::Stop);
}

TEST(PlayerStateMachineTest, GetStateFromErrorEvents) {
    auto readError = PlayerStateMachine::getStateFromEvent(EventType::ReadError);
    ASSERT_TRUE(readError.has_value());
    EXPECT_EQ(readError.value(), PlayerState::Error);

    auto decodeError = PlayerStateMachine::getStateFromEvent(EventType::DecodeError);
    ASSERT_TRUE(decodeError.has_value());
    EXPECT_EQ(decodeError.value(), PlayerState::Error);

    auto demuxError = PlayerStateMachine::getStateFromEvent(EventType::DemuxError);
    ASSERT_TRUE(demuxError.has_value());
    EXPECT_EQ(demuxError.value(), PlayerState::Error);

    auto renderError = PlayerStateMachine::getStateFromEvent(EventType::RenderError);
    ASSERT_TRUE(renderError.has_value());
    EXPECT_EQ(renderError.value(), PlayerState::Error);
}

TEST(PlayerStateMachineTest, GetStateFromUnknownEvent) {
    auto result = PlayerStateMachine::getStateFromEvent(EventType::Unknown);
    EXPECT_FALSE(result.has_value());
}

TEST(PlayerStateMachineTest, GetStateFromUnmappedEvent) {
    // Seek、Prepared 等不在映射表中
    auto seekResult = PlayerStateMachine::getStateFromEvent(EventType::Seek);
    EXPECT_FALSE(seekResult.has_value());
}
