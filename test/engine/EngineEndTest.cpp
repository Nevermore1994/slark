//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <chrono>

#include "Fakes.h"

using namespace std::chrono_literals;
using namespace slark;
using namespace slark::engine;

namespace {

Time::TimePoint sec(double s) {
    return Time::TimePoint::fromSeconds(s);
}

}  // namespace

class EngineEndTest : public fake::EngineTest {};

TEST_F(EngineEndTest, AllPipelinesEndedYieldsEndedAndClearsWantPlay) {
    openReady();
    send(Play{});
    ASSERT_TRUE(engine->snapshot().isPlaying);
    pipeline().end();
    sched.runUntilIdle();

    const auto snap = engine->snapshot();
    EXPECT_EQ(snap.status, Status::Ended);
    EXPECT_FALSE(snap.wantPlay);
    EXPECT_FALSE(snap.isPlaying);
    EXPECT_EQ(snap.position, sec(10));
}

TEST_F(EngineEndTest, PlayEndIsReportedOncePerEnding) {
    openReady();
    send(Play{});
    pipeline().end();
    sched.runUntilIdle();
    ASSERT_EQ(status(), Status::Ended);
    send(Pause{});
    EXPECT_EQ(rec->count(PlayerEvent::PlayEnd), 1);

    send(Play{});
    EXPECT_EQ(rec->count(PlayerEvent::PlayEnd), 1);
}

TEST_F(EngineEndTest, EveryLoopLapReportsPlayEndWithoutLeavingTheSession) {
    openReady();
    send(SetRepeat{RepeatMode::One});
    send(Play{});
    pipeline().end();
    sched.runUntilIdle();
    EXPECT_EQ(rec->count(PlayerEvent::PlayEnd), 1);
    EXPECT_NE(status(), Status::Ended);
}

TEST_F(EngineEndTest, OneEndedPipelineIsNotEnough) {
    factory->pipelineBlocking = {true, true};
    openReady();
    pipeline(1).becomeReady();
    pipeline(0).end();
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Ready);
    pipeline(1).end();
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Ended);
}

TEST_F(EngineEndTest, SeekingAfterEndDoesNotStartPlayback) {
    openReady();
    send(Play{});
    pipeline().end();
    sched.runUntilIdle();
    ASSERT_EQ(status(), Status::Ended);

    send(Seek{sec(2), SeekMode::Exact});
    src().setCache(9.0);
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Ready);
    EXPECT_FALSE(engine->snapshot().isPlaying);
    EXPECT_EQ(rec->count(PlayerEvent::SeekDone), 1);
}

TEST_F(EngineEndTest, PlayAfterEndRestartsFromTheBeginning) {
    openReady();
    send(Play{});
    pipeline().end();
    sched.runUntilIdle();
    ASSERT_EQ(status(), Status::Ended);

    send(Play{});
    ASSERT_EQ(pipeline().flushes.size(), 1u);
    EXPECT_EQ(pipeline().flushes[0].target, sec(0));
    EXPECT_EQ(pipeline().flushes[0].mode, SeekMode::Exact);

    src().setCache(6.0);
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_TRUE(engine->snapshot().isPlaying);
    EXPECT_EQ(rec->count(PlayerEvent::SeekDone), 0);
}

TEST_F(EngineEndTest, RepeatRestartsInsteadOfEnding) {
    openReady();
    send(SetRepeat{RepeatMode::One});
    send(Play{});
    pipeline().end();
    sched.runUntilIdle();

    ASSERT_EQ(pipeline().flushes.size(), 1u);
    EXPECT_EQ(pipeline().flushes[0].target, sec(0));
    EXPECT_NE(status(), Status::Ended);
    EXPECT_TRUE(engine->snapshot().wantPlay);
    EXPECT_EQ(rec->count(PlayerEvent::SeekDone), 0);

    src().setCache(6.0);
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_TRUE(engine->snapshot().isPlaying);
}

TEST_F(EngineEndTest, RepeatFromPrepareSettingLoops) {
    Prepare p;
    p.setting.isLoop = true;
    prepare(p);
    src().setTracks(10.0);
    src().setCache(5.0);
    sched.runUntilIdle();
    pipeline().becomeReady();
    sched.runUntilIdle();
    pipeline().end();
    sched.runUntilIdle();
    EXPECT_EQ(pipeline().flushes.size(), 1u);
}

TEST_F(EngineEndTest, EndedIsNotReportedAsPlayingEvenIfPlayArrivesAfterwards) {
    openReady();
    pipeline().end();
    sched.runUntilIdle();
    ASSERT_EQ(status(), Status::Ended);
    send(Pause{});
    EXPECT_FALSE(engine->snapshot().isPlaying);
}

TEST_F(EngineEndTest, ObserverSeesCompletedWithDurationAsTime) {
    openReady();
    send(Play{});
    pipeline().end();
    sched.runUntilIdle();
    ASSERT_FALSE(rec->states.empty());
    EXPECT_EQ(rec->states.back(), PlayerState::Completed);
    EXPECT_DOUBLE_EQ(rec->times.back(), 10.0);
}
