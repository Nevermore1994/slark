//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <chrono>

#include "Fakes.h"

using namespace std::chrono_literals;
using namespace slark;
using namespace slark::engine;

class EngineStartupTest : public fake::EngineTest {};

TEST_F(EngineStartupTest, IdleBeforePrepare) {
    EXPECT_EQ(status(), Status::Idle);
}

TEST_F(EngineStartupTest, OpeningUntilTheTracksAreKnown) {
    prepare();
    EXPECT_EQ(status(), Status::Opening);
    src().setTracks(10.0);
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Buffering);
}

TEST_F(EngineStartupTest, PictureComesBeforeReadyAndTheClockWaitsForTheStartBuffer) {
    open();
    send(Play{});
    pipeline().becomeReady();
    src().setCache(1.0);
    sched.runUntilIdle();

    EXPECT_TRUE(pipeline().ready(pipeline().currentEpoch, {}));
    EXPECT_EQ(status(), Status::Buffering);
    EXPECT_FALSE(engine->snapshot().isPlaying);

    src().setCache(2.0);
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Ready);
    EXPECT_TRUE(engine->snapshot().isPlaying);
}

TEST_F(EngineStartupTest, ClockRunsOnlyAfterReady) {
    open();
    send(Play{});
    pipeline().becomeReady();
    src().setCache(1.0);
    sched.advance(1s);
    EXPECT_EQ(engine->snapshot().position, Time::TimePoint(0));

    src().setCache(9.0);
    sched.advance(1s);
    EXPECT_EQ(engine->snapshot().position, Time::TimePoint::fromSeconds(1.0));
}

TEST_F(EngineStartupTest, SingleBlockingPipelineIsEnoughToBeReady) {
    open();
    src().setCache(3.0);
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Ready);
}

TEST_F(EngineStartupTest, NonBlockingPipelineDoesNotHoldReadiness) {
    factory->pipelineBlocking = {true, false};
    open();
    src().setCache(3.0);
    pipeline(0).becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Ready);
}

TEST_F(EngineStartupTest, EveryBlockingPipelineMustBeReady) {
    factory->pipelineBlocking = {true, true};
    open();
    src().setCache(3.0);
    pipeline(0).becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Buffering);
    pipeline(1).becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Ready);
}

TEST_F(EngineStartupTest, PlayBeforeReadyIsKeptAndStartsAutomatically) {
    prepare();
    send(Play{});
    EXPECT_TRUE(engine->snapshot().wantPlay);
    src().setTracks(10.0);
    src().setCache(3.0);
    sched.runUntilIdle();
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_TRUE(engine->snapshot().isPlaying);
}

TEST_F(EngineStartupTest, PlayBeforePrepareIsKept) {
    send(Play{});
    EXPECT_TRUE(engine->snapshot().wantPlay);
    EXPECT_EQ(rec->count(PlayerEvent::OnError), 0);
    prepare();
    EXPECT_TRUE(engine->snapshot().wantPlay);
}

TEST_F(EngineStartupTest, ZeroStartBufferIsReadyWithoutCache) {
    Prepare p;
    FlowConfig flow;
    flow.buffering.startBufferSec = 0.0;
    p.setting.flowConfig = flow;
    prepare(p);
    src().setTracks(10.0);
    sched.runUntilIdle();
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Ready);
}

TEST_F(EngineStartupTest, FullyLoadedShortMediaDoesNotWaitForTheStartBuffer) {
    open(1.0);
    src().loading = false;
    src().setCache(0.5);
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Ready);
}

TEST_F(EngineStartupTest, RebufferingHasADeadBand) {
    openReady();
    ASSERT_EQ(status(), Status::Ready);

    src().setCache(2.9);
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Buffering);

    src().setCache(4.4);
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Buffering);

    src().setCache(4.5);
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Ready);
}

TEST_F(EngineStartupTest, RebufferingStopsTheClockAndResumesWithIt) {
    openReady();
    send(Play{});
    sched.advance(1s);
    src().setCache(1.2);
    sched.runUntilIdle();
    ASSERT_EQ(status(), Status::Buffering);

    const auto frozen = engine->snapshot().position;
    sched.advance(2s);
    EXPECT_EQ(engine->snapshot().position, frozen);

    src().setCache(9.0);
    sched.advance(1s);
    EXPECT_EQ(engine->snapshot().position, frozen + Time::TimeDelta::fromMilliSeconds(1000ms));
}

TEST_F(EngineStartupTest, SourceFailureBecomesFailedAndNotifiesOnce) {
    open();
    src().fail = Error::StreamInvalidData;
    src().wake();
    sched.runUntilIdle();
    sched.advance(1s);
    EXPECT_EQ(status(), Status::Failed);
    EXPECT_EQ(rec->count(PlayerEvent::OnError), 1);
    EXPECT_EQ(rec->events.back().second,
              std::to_string(static_cast<uint32_t>(Error::StreamInvalidData)) + "|0|");
}

TEST_F(EngineStartupTest, SourceFailureKeepsTheOriginCodeAndTheHost) {
    open();
    src().fail = Error::HttpError;
    src().failDetail = ErrorDetail{503, "cdn.example.com:8443"};
    src().wake();
    sched.runUntilIdle();
    ASSERT_EQ(rec->count(PlayerEvent::OnError), 1);
    EXPECT_EQ(rec->events.back().second,
              std::to_string(static_cast<uint32_t>(Error::HttpError)) + "|503|cdn.example.com:8443");
}

TEST_F(EngineStartupTest, PipelineFailureBecomesFailed) {
    open();
    pipeline().fail = Error::CodecError;
    pipeline().wake();
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Failed);
}

TEST_F(EngineStartupTest, NoUsablePipelineIsAFormatError) {
    factory->emptyPipelines = true;
    open();
    EXPECT_EQ(status(), Status::Failed);
    EXPECT_EQ(rec->events.back().second,
              std::to_string(static_cast<uint32_t>(Error::PipelineFormatNotSupported)) + "|0|");
}

TEST_F(EngineStartupTest, SourceCreationFailureFails) {
    factory->nullSource = true;
    prepare();
    EXPECT_EQ(status(), Status::Failed);
}

TEST_F(EngineStartupTest, SourceIsRegulatedWithTheClockPosition) {
    openReady();
    ASSERT_FALSE(src().regulated.empty());
    const auto before = src().regulated.size();
    src().wake();
    sched.runUntilIdle();
    EXPECT_GT(src().regulated.size(), before);
}

TEST_F(EngineStartupTest, ManyWakeupsCollapseIntoOneReconcile) {
    openReady();
    const auto before = src().regulated.size();
    for (int i = 0; i < 20; ++i) {
        src().wake();
    }
    sched.runUntilIdle();
    EXPECT_EQ(src().regulated.size(), before + 1);
}

TEST_F(EngineStartupTest, PipelineDeadlineWakesTheEngine) {
    openReady();
    const auto before = src().regulated.size();
    pipeline().wakeup = sched.now() + 50ms;
    src().wake();
    sched.runUntilIdle();
    const auto afterWake = src().regulated.size();
    EXPECT_GT(afterWake, before);
    sched.advance(60ms);
    EXPECT_GT(src().regulated.size(), afterWake);
}

TEST_F(EngineStartupTest, PausedAndFullyLoadedSchedulesNoTimer) {
    openReady();
    src().loading = false;
    src().wake();
    sched.runUntilIdle();
    EXPECT_EQ(sched.pendingTimers(), 0u);
}
