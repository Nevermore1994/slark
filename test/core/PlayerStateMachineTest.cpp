//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "PlayerStateMachine.h"

#include <gtest/gtest.h>

using namespace slark;


TEST(PlayerStateMachineTest, NotInitedOnlyToInitializing) {
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::NotInited, PlayerState::Initializing));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::NotInited, PlayerState::Prepared));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::NotInited, PlayerState::Playing));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::NotInited, PlayerState::Stop));
}

TEST(PlayerStateMachineTest, InitializingValidTargets) {
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

TEST(PlayerStateMachineTest, PreparedValidTargets) {
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::Prepared, PlayerState::Buffering));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Prepared, PlayerState::Stop));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Prepared, PlayerState::Error));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::Prepared, PlayerState::Playing));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::Prepared, PlayerState::Completed));
}

TEST(PlayerStateMachineTest, BufferingValidTargets) {
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Buffering, PlayerState::Ready));
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::Buffering, PlayerState::Playing));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Buffering, PlayerState::Pause));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Buffering, PlayerState::Stop));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Buffering, PlayerState::Error));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::Buffering, PlayerState::Completed));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::Buffering, PlayerState::NotInited));
}

TEST(PlayerStateMachineTest, ReadyValidTargets) {
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Ready, PlayerState::Playing));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Ready, PlayerState::Buffering));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Ready, PlayerState::Stop));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Ready, PlayerState::Error));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Ready, PlayerState::Pause));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Ready, PlayerState::Completed));
}

TEST(PlayerStateMachineTest, PlayingValidTargets) {
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Playing, PlayerState::Pause));
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::Playing, PlayerState::Buffering));
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::Playing, PlayerState::Completed));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Playing, PlayerState::Stop));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Playing, PlayerState::Error));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Playing, PlayerState::Ready));
    EXPECT_FALSE(
        PlayerStateMachine::isValidTransition(PlayerState::Playing, PlayerState::NotInited));
}

TEST(PlayerStateMachineTest, PauseValidTargets) {
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Pause, PlayerState::Playing));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Pause, PlayerState::Buffering));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Pause, PlayerState::Stop));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Pause, PlayerState::Error));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Pause, PlayerState::Ready));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Pause, PlayerState::Completed));
}

TEST(PlayerStateMachineTest, CompletedValidTransitions) {
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::Completed, PlayerState::Playing));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Completed, PlayerState::Pause));
    EXPECT_TRUE(
        PlayerStateMachine::isValidTransition(PlayerState::Completed, PlayerState::Buffering));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Completed, PlayerState::Stop));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Completed, PlayerState::Error));
}

TEST(PlayerStateMachineTest, StopHasNoValidTransition) {
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Stop, PlayerState::Playing));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Stop, PlayerState::Pause));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Stop, PlayerState::Error));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Stop, PlayerState::NotInited));
}

TEST(PlayerStateMachineTest, ErrorOnlyToStop) {
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(PlayerState::Error, PlayerState::Stop));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Error, PlayerState::Playing));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Error, PlayerState::Pause));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(PlayerState::Error, PlayerState::Error));
}


TEST(PlayerStateMachineTest, EventPlayMapsToPlaying) {
    auto result = PlayerStateMachine::getStateFromEvent(EventType::Play);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, PlayerState::Playing);
}

TEST(PlayerStateMachineTest, EventPauseMappsToPause) {
    auto result = PlayerStateMachine::getStateFromEvent(EventType::Pause);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, PlayerState::Pause);
}

TEST(PlayerStateMachineTest, EventStopMapsToStop) {
    auto result = PlayerStateMachine::getStateFromEvent(EventType::Stop);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, PlayerState::Stop);
}

TEST(PlayerStateMachineTest, ErrorEventsMapsToError) {
    for (auto ev : {EventType::ReadError, EventType::DecodeError, EventType::DemuxError,
                    EventType::RenderError}) {
        auto result = PlayerStateMachine::getStateFromEvent(ev);
        ASSERT_TRUE(result.has_value()) << "event=" << static_cast<int32_t>(ev);
        EXPECT_EQ(*result, PlayerState::Error) << "event=" << static_cast<int32_t>(ev);
    }
}

TEST(PlayerStateMachineTest, UnknownEventMapsToUnknown) {
    auto result = PlayerStateMachine::getStateFromEvent(EventType::Unknown);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, PlayerState::Unknown);
}

TEST(PlayerStateMachineTest, NonStatefulEventsReturnUnexpected) {
    for (auto ev : {EventType::Seek, EventType::UpdateSetting, EventType::UpdateSettingVolume,
                    EventType::UpdateSettingMute, EventType::UpdateSettingEnd}) {
        auto result = PlayerStateMachine::getStateFromEvent(ev);
        EXPECT_FALSE(result.has_value()) << "event=" << static_cast<int32_t>(ev);
    }
}


TEST(PlayerStateMachineTest, PipelineDecoderFailedMapsToError) {
    for (auto ev : {PipelineEvent::AudioDecoderFailed, PipelineEvent::VideoDecoderFailed,
                    PipelineEvent::ReadError, PipelineEvent::DecodeError}) {
        auto result = PlayerStateMachine::getStateFromPipelineEvent(ev);
        ASSERT_TRUE(result.has_value()) << "event=" << static_cast<int32_t>(ev);
        EXPECT_EQ(*result, PlayerState::Error) << "event=" << static_cast<int32_t>(ev);
    }
}

TEST(PlayerStateMachineTest, PipelineBufferingStartMapsToBuffering) {
    auto result = PlayerStateMachine::getStateFromPipelineEvent(PipelineEvent::BufferingStart);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, PlayerState::Buffering);
}

TEST(PlayerStateMachineTest, PipelineRenderEndMapsToCompleted) {
    auto result = PlayerStateMachine::getStateFromPipelineEvent(PipelineEvent::RenderEnd);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, PlayerState::Completed);
}

TEST(PlayerStateMachineTest, PipelineNonStatefulEventsReturnUnexpected) {
    for (auto ev : {PipelineEvent::DemuxHeaderParsed, PipelineEvent::PipelineReady,
                    PipelineEvent::FirstFrameRendered, PipelineEvent::BufferingEnd,
                    PipelineEvent::SeekCompleted}) {
        auto result = PlayerStateMachine::getStateFromPipelineEvent(ev);
        EXPECT_FALSE(result.has_value()) << "event=" << static_cast<int32_t>(ev);
    }
}


TEST(PlayerStateMachineTest, NormalPlaybackPath) {
    using S = PlayerState;
    const std::vector<std::pair<S, S>> path = {
        {S::NotInited, S::Initializing}, {S::Initializing, S::Prepared},
        {S::Prepared, S::Buffering},     {S::Buffering, S::Playing},
        {S::Playing, S::Completed},      {S::Completed, S::Stop},
    };
    for (const auto& [from, to] : path) {
        EXPECT_TRUE(PlayerStateMachine::isValidTransition(from, to))
            << "from=" << static_cast<int32_t>(from) << " to=" << static_cast<int32_t>(to);
    }
}

TEST(PlayerStateMachineTest, PauseResumePath) {
    using S = PlayerState;
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(S::Playing, S::Pause));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(S::Pause, S::Playing));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(S::Playing, S::Stop));
}

TEST(PlayerStateMachineTest, RebufferingPath) {
    using S = PlayerState;
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(S::Playing, S::Buffering));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(S::Buffering, S::Playing));
}

TEST(PlayerStateMachineTest, ErrorRecoveryPath) {
    using S = PlayerState;
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(S::Playing, S::Error));
    EXPECT_TRUE(PlayerStateMachine::isValidTransition(S::Error, S::Stop));
    EXPECT_FALSE(PlayerStateMachine::isValidTransition(S::Error, S::Playing));
}
