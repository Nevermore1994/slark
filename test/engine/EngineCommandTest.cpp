//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <atomic>
#include <chrono>
#include <thread>

#include "Fakes.h"

using namespace std::chrono_literals;
using namespace slark;
using namespace slark::engine;

namespace {

Time::TimePoint sec(double s) {
    return Time::TimePoint::fromSeconds(s);
}

}  // namespace

class EngineCommandTest : public fake::EngineTest {};

TEST_F(EngineCommandTest, PlayAndPauseInIdleAreIntentNotErrors) {
    send(Play{});
    EXPECT_TRUE(engine->snapshot().wantPlay);
    send(Pause{});
    EXPECT_FALSE(engine->snapshot().wantPlay);
    EXPECT_EQ(rec->count(PlayerEvent::OnError), 0);
}

TEST_F(EngineCommandTest, PlayAndPauseAfterFailureAreRejected) {
    open();
    src().fail = Error::StreamInvalidData;
    src().wake();
    sched.runUntilIdle();
    const auto errors = rec->count(PlayerEvent::OnError);
    send(Play{});
    send(Pause{});
    EXPECT_EQ(rec->count(PlayerEvent::OnError), errors + 2);
    EXPECT_FALSE(engine->snapshot().wantPlay);
}

TEST_F(EngineCommandTest, CloseTurnsCommandsQueuedBeforeItIntoNoOps) {
    Prepare prepare;
    prepare.item.path = "fake";
    engine->post(std::move(prepare));
    engine->close();
    sched.runUntilIdle();
    EXPECT_EQ(factory->sourcesCreated, 0);
    EXPECT_EQ(status(), Status::Idle);
}

TEST_F(EngineCommandTest, FailedClearsWantPlay) {
    open();
    send(Play{});
    src().fail = Error::StreamInvalidData;
    src().wake();
    sched.runUntilIdle();
    EXPECT_FALSE(engine->snapshot().wantPlay);
}

TEST_F(EngineCommandTest, SetRenderSuspendedFreezesAndResumesTheClock) {
    openReady();
    send(Play{});
    sched.advance(1s);
    const auto before = engine->snapshot().position;
    ASSERT_GT(before.second(), 0.0);

    send(SetRenderSuspended{true});
    sched.advance(2s);
    EXPECT_TRUE(engine->snapshot().renderSuspended);
    EXPECT_EQ(engine->snapshot().position, before);

    send(SetRenderSuspended{false});
    sched.advance(1s);
    EXPECT_FALSE(engine->snapshot().renderSuspended);
    EXPECT_GT(engine->snapshot().position.second(), before.second());
}

TEST_F(EngineCommandTest, VsyncFromTheRenderThreadSurvivesRepeatedTeardown) {
    std::atomic<bool> stop{false};
    std::thread render([&] {
        while (!stop) {
            engine->notifyVsync();
        }
    });
    for (int i = 0; i < 300; ++i) {
        open();
        send(Stop{});
    }
    stop = true;
    render.join();
    EXPECT_EQ(status(), Status::Idle);
}

TEST_F(EngineCommandTest, FirstFrameRenderedIsReportedOncePerSession) {
    open();
    EXPECT_EQ(rec->count(PlayerEvent::FirstFrameRendered), 0);
    pipeline().present();
    sched.runUntilIdle();
    pipeline().present();
    sched.runUntilIdle();
    EXPECT_EQ(rec->count(PlayerEvent::FirstFrameRendered), 1);

    send(Stop{});
    open();
    pipeline().present();
    sched.runUntilIdle();
    EXPECT_EQ(rec->count(PlayerEvent::FirstFrameRendered), 2);
}

TEST_F(EngineCommandTest, MuteSilencesThePipelinesAndUnmuteRestoresTheVolume) {
    open();
    const auto lastVolume = [this] {
        return pipeline().volumes.empty() ? -1.0f : pipeline().volumes.back();
    };
    send(SetVolume{0.4f});
    EXPECT_FLOAT_EQ(lastVolume(), 0.4f);
    send(SetMute{true});
    EXPECT_FLOAT_EQ(lastVolume(), 0.0f);
    send(SetVolume{0.7f});
    EXPECT_FLOAT_EQ(lastVolume(), 0.0f);
    send(SetMute{false});
    EXPECT_FLOAT_EQ(lastVolume(), 0.7f);
}

TEST_F(EngineCommandTest, VolumeIsClampedAndAppliedToPipelinesCreatedLater) {
    send(SetVolume{3.0f});
    EXPECT_FLOAT_EQ(engine->snapshot().volume, 1.0f);
    send(SetVolume{0.25f});
    send(SetMute{true});
    open();
    ASSERT_FALSE(pipeline().volumes.empty());
    EXPECT_FLOAT_EQ(pipeline().volumes.front(), 0.0f);
}

TEST_F(EngineCommandTest, SeekabilityAndCacheFollowTheMediaFacts) {
    prepare();
    EXPECT_FALSE(engine->snapshot().isSeekable);
    src().setTracks(10.0, /*seekable=*/false);
    sched.runUntilIdle();
    EXPECT_FALSE(engine->snapshot().isSeekable);

    send(Stop{});
    prepare();
    src().setTracks(10.0, /*seekable=*/true);
    src().setCache(12.5);
    sched.runUntilIdle();
    const auto snap = engine->snapshot();
    EXPECT_TRUE(snap.isSeekable);
    EXPECT_EQ(snap.buffered, sec(10));
}

TEST_F(EngineCommandTest, CacheTimeIsRateLimitedAndTheLastValueArrivesWithoutAnotherTrigger) {
    openReady();
    src().loading = false;
    sched.advance(1s);
    const auto reported = [this] {
        return rec->count(PlayerEvent::UpdateCacheTime);
    };
    const auto before = reported();

    for (int i = 1; i <= 6; ++i) {
        src().setCache(5.0 + 0.3 * i);
        src().wake();
        sched.runUntilIdle();
    }
    EXPECT_LE(reported(), before + 1);

    sched.advance(600ms);
    EXPECT_GT(reported(), before);
    EXPECT_EQ(rec->events.back().first, PlayerEvent::UpdateCacheTime);
    EXPECT_EQ(rec->events.back().second, std::to_string(6.8));

    const auto settled = reported();
    sched.advance(5s);
    EXPECT_EQ(reported(), settled);
    EXPECT_EQ(sched.pendingTimers(), 0u);
}

TEST_F(EngineCommandTest, StopReturnsToIdleFromAnyPhase) {
    open();
    send(Play{});
    send(Stop{});
    const auto snap = engine->snapshot();
    EXPECT_EQ(snap.status, Status::Idle);
    EXPECT_FALSE(snap.wantPlay);
    EXPECT_EQ(snap.position, Time::TimePoint(0));
    EXPECT_EQ(snap.duration, Time::TimePoint(0));
    EXPECT_EQ(sched.pendingTimers(), 0u);
}

TEST_F(EngineCommandTest, StopClosesTheSourceAndRecoversFromFailure) {
    open();
    src().fail = Error::StreamInvalidData;
    src().wake();
    sched.runUntilIdle();
    ASSERT_EQ(status(), Status::Failed);

    send(Stop{});
    EXPECT_EQ(status(), Status::Idle);
    EXPECT_EQ(factory->sourcesClosed, 1);
    prepare();
    EXPECT_EQ(factory->sourcesCreated, 2);
    EXPECT_EQ(status(), Status::Opening);
}

TEST_F(EngineCommandTest, StopWhileIdleIsHarmless) {
    send(Stop{});
    EXPECT_EQ(status(), Status::Idle);
    EXPECT_EQ(rec->count(PlayerEvent::OnError), 0);
}

TEST_F(EngineCommandTest, ReleaseRejectsEveryLaterCommand) {
    open();
    send(Release{});
    EXPECT_EQ(status(), Status::Idle);

    send(Play{});
    send(Pause{});
    send(Seek{sec(1), SeekMode::Exact});
    send(Stop{});
    send(Prepare{});
    send(SetVolume{0.5f});
    EXPECT_EQ(rec->count(PlayerEvent::OnError), 6);
    EXPECT_EQ(factory->sourcesCreated, 1);
}

TEST_F(EngineCommandTest, ReleaseIsIdempotent) {
    send(Release{});
    send(Release{});
    EXPECT_EQ(rec->count(PlayerEvent::OnError), 0);
}

TEST_F(EngineCommandTest, ReleaseClosesTheSource) {
    open();
    send(Release{});
    EXPECT_EQ(factory->sourcesClosed, 1);
}

TEST_F(EngineCommandTest, PrepareWhenNotIdleIsRejected) {
    prepare();
    prepare();
    EXPECT_EQ(rec->count(PlayerEvent::OnError), 1);
    EXPECT_EQ(factory->sourcesCreated, 1);
}

TEST_F(EngineCommandTest, PrepareAfterStopStartsAFreshSession) {
    openReady();
    send(Stop{});
    open();
    EXPECT_EQ(factory->sourcesCreated, 2);
    EXPECT_EQ(status(), Status::Buffering);
}

TEST_F(EngineCommandTest, SpeedChangesHowFastThePositionAdvances) {
    openReady();
    send(Play{});
    send(SetSpeed{2.0});
    sched.advance(1s);
    EXPECT_EQ(engine->snapshot().position, sec(2));
    EXPECT_DOUBLE_EQ(engine->snapshot().speed, 2.0);
}

TEST_F(EngineCommandTest, InvalidSpeedIsRejected) {
    send(SetSpeed{0.0});
    send(SetSpeed{-1.0});
    EXPECT_EQ(rec->count(PlayerEvent::OnError), 2);
    EXPECT_DOUBLE_EQ(engine->snapshot().speed, 1.0);
}

TEST_F(EngineCommandTest, VolumeAndMuteAreIndependentFacts) {
    send(SetVolume{0.3f});
    send(SetMute{true});
    auto snap = engine->snapshot();
    EXPECT_FLOAT_EQ(snap.volume, 0.3f);
    EXPECT_TRUE(snap.muted);
    send(SetMute{false});
    snap = engine->snapshot();
    EXPECT_FLOAT_EQ(snap.volume, 0.3f);
    EXPECT_FALSE(snap.muted);
}

TEST_F(EngineCommandTest, PrepareAppliesTheInitialVolume) {
    Prepare p;
    p.setting.volume = 50.0f;
    prepare(p);
    EXPECT_FLOAT_EQ(engine->snapshot().volume, 0.5f);
}

TEST_F(EngineCommandTest, StopClearsTheSessionFacts) {
    open();
    send(SetSpeed{2.0});
    send(Stop{});
    EXPECT_DOUBLE_EQ(engine->snapshot().speed, 1.0);
}

TEST_F(EngineCommandTest, ObserverSeesTheLifecycle) {
    openReady();
    send(Play{});
    send(Pause{});
    send(Stop{});
    // Prepared comes once tracks are known, then Buffering until the first frame is presented.
    EXPECT_EQ(rec->states, (std::vector<PlayerState>{
                               PlayerState::Buffering, PlayerState::Prepared, PlayerState::Buffering,
                               PlayerState::Pause, PlayerState::Playing, PlayerState::Pause,
                               PlayerState::NotInited}));
}

TEST_F(EngineCommandTest, SnapshotReportsLoadingAndBuffered) {
    open();
    src().setCache(4.0);
    sched.runUntilIdle();
    auto snap = engine->snapshot();
    EXPECT_TRUE(snap.isLoading);
    EXPECT_EQ(snap.buffered, sec(4));
    EXPECT_EQ(snap.duration, sec(10));
}

TEST_F(EngineCommandTest, CommandsPostedFromAnotherThreadRunOnTheScheduler) {
    std::thread([&] {
        engine->post(Prepare{});
    }).join();
    EXPECT_EQ(status(), Status::Idle);
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Opening);
}

TEST_F(EngineCommandTest, WakeAfterTheEngineIsGoneIsHarmless) {
    open();
    auto wake = src().wake;
    engine.reset();
    wake();
    sched.runUntilIdle();
}

TEST_F(EngineCommandTest, DebugInfoEnablesSnapshotAndReportsPlayback) {
    openReady();
    const auto info = engine->debugInfo();
    EXPECT_FALSE(info.playback.empty());
    EXPECT_NE(info.playback.find("Status: Ready"), std::string::npos);
    EXPECT_NE(info.playback.find("Duration: 10 s"), std::string::npos);
    EXPECT_FALSE(info.buffer.empty());
    EXPECT_NE(info.buffer.find("Cache time:"), std::string::npos);
    EXPECT_TRUE(info.source.empty());
}

TEST_F(EngineCommandTest, DebugInfoSurvivesTeardown) {
    openReady();
    (void)engine->debugInfo();
    send(Stop{});
    sched.runUntilIdle();
    const auto info = engine->debugInfo();
    EXPECT_FALSE(info.playback.empty());
    EXPECT_NE(info.playback.find("Status: Idle"), std::string::npos);
}

TEST_F(EngineCommandTest, TeardownHookRunsAfterSourceIsClosed) {
    int closesAtHook = -1;
    int hookCalls = 0;
    engine = Engine::create(sched, notifier, fakeFactory.hooks(), [&] {
        ++hookCalls;
        closesAtHook = factory->sourcesClosed;
    });
    prepare();
    send(Stop{});
    EXPECT_EQ(hookCalls, 1);
    EXPECT_EQ(closesAtHook, 1);
}
