//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <chrono>
#include <string>
#include <vector>

#include "Fakes.h"

using namespace std::chrono_literals;
using namespace slark;
using namespace slark::engine;

namespace {

Time::TimePoint sec(double s) {
    return Time::TimePoint::fromSeconds(s);
}

}  // namespace

class EngineSeekTest : public fake::EngineTest {};

TEST_F(EngineSeekTest, TwoSeeksProduceOneSeekDone) {
    openReady();
    send(Seek{sec(5), SeekMode::Exact});
    send(Seek{sec(7), SeekMode::Exact});
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(rec->count(PlayerEvent::SeekDone), 1);
}

TEST_F(EngineSeekTest, ReadinessOfASupersededEpochDoesNotCompleteTheNewSeek) {
    openReady();
    send(Seek{sec(5), SeekMode::Exact});
    const auto first = pipeline().currentEpoch;
    send(Seek{sec(7), SeekMode::Exact});
    const auto second = pipeline().currentEpoch;
    ASSERT_NE(first, second);

    pipeline().becomeReady(first);
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Seeking);
    EXPECT_EQ(rec->count(PlayerEvent::SeekDone), 0);

    pipeline().becomeReady(second);
    sched.runUntilIdle();
    EXPECT_EQ(rec->count(PlayerEvent::SeekDone), 1);
}

TEST_F(EngineSeekTest, SourceAndPipelinesReceiveTheSameEpochAndTarget) {
    factory->pipelineBlocking = {true, true};
    openReady();
    pipeline(1).becomeReady();
    send(Seek{sec(5), SeekMode::Fast});
    ASSERT_EQ(src().seeks.size(), 1u);
    EXPECT_EQ(src().seeks[0].target, sec(5));
    for (size_t i = 0; i < 2; ++i) {
        ASSERT_EQ(pipeline(i).flushes.size(), 1u);
        EXPECT_EQ(pipeline(i).flushes[0].epoch, src().seeks[0].epoch);
        EXPECT_EQ(pipeline(i).flushes[0].target, sec(5));
    }
}

// Pipelines must flush before the source seeks, or its new-epoch keyframe is dropped as stale.
TEST_F(EngineSeekTest, PipelinesSwitchEpochBeforeTheSourceSeeks) {
    factory->pipelineBlocking = {true, true};
    openReady();
    pipeline(1).becomeReady();

    std::vector<std::string> order;
    pipeline(0).onFlush = [&order] {
        order.emplace_back("pipeline0");
    };
    pipeline(1).onFlush = [&order] {
        order.emplace_back("pipeline1");
    };
    src().onSeek = [&order] {
        order.emplace_back("source");
    };

    send(Seek{sec(5), SeekMode::Exact});
    ASSERT_EQ(src().seeks.size(), 1u);
    const std::vector<std::string> expected{"pipeline0", "pipeline1", "source"};
    EXPECT_EQ(order, expected);
}

TEST_F(EngineSeekTest, StalledSeekRetryAlsoFlushesPipelinesBeforeSeekingTheSource) {
    openReady();

    std::vector<std::string> order;
    pipeline().onFlush = [&order] {
        order.emplace_back("pipeline");
    };
    src().onSeek = [&order] {
        order.emplace_back("source");
    };

    send(Seek{sec(5), SeekMode::Exact});
    sched.advance(3500ms);
    ASSERT_EQ(src().seeks.size(), 2u);
    const std::vector<std::string> expected{"pipeline", "source", "pipeline", "source"};
    EXPECT_EQ(order, expected);
}

TEST_F(EngineSeekTest, ExhaustedSeekRetriesFailLoudlyAndReleaseTheSource) {
    openReady();
    send(Seek{sec(5), SeekMode::Exact});
    for (int i = 0; i < 5; ++i) {
        sched.advance(3500ms);
    }
    EXPECT_EQ(status(), Status::Failed);
    EXPECT_EQ(rec->count(PlayerEvent::OnError), 1);
    ASSERT_FALSE(src().seekingCalls.empty());
    EXPECT_FALSE(src().seekingCalls.back());
}

TEST_F(EngineSeekTest, ModeReachesThePipeline) {
    openReady();
    send(Seek{sec(5), SeekMode::Fast});
    send(Seek{sec(6), SeekMode::Exact});
    ASSERT_EQ(pipeline().flushes.size(), 2u);
    EXPECT_EQ(pipeline().flushes[0].mode, SeekMode::Fast);
    EXPECT_EQ(pipeline().flushes[1].mode, SeekMode::Exact);
}

TEST_F(EngineSeekTest, SeekingStopsTheClockAndMovesPositionToTheTarget) {
    openReady();
    send(Play{});
    sched.advance(1s);
    send(Seek{sec(8), SeekMode::Exact});
    EXPECT_EQ(status(), Status::Seeking);
    EXPECT_EQ(engine->snapshot().position, sec(8));
    sched.advance(2s);
    EXPECT_EQ(engine->snapshot().position, sec(8));
}

TEST_F(EngineSeekTest, SeekDoneComesBeforeTheStartBufferIsSatisfied) {
    openReady();
    send(Seek{sec(5), SeekMode::Exact});
    src().setCache(5.5);
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(rec->count(PlayerEvent::SeekDone), 1);
    EXPECT_EQ(status(), Status::Buffering);

    src().setCache(7.0);
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Ready);
}

TEST_F(EngineSeekTest, SeekWhilePausedStaysPaused) {
    openReady();
    send(Seek{sec(5), SeekMode::Exact});
    src().setCache(9.0);
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(status(), Status::Ready);
    EXPECT_FALSE(engine->snapshot().isPlaying);
}

TEST_F(EngineSeekTest, SeekWhilePlayingResumesWhenReady) {
    openReady();
    send(Play{});
    send(Seek{sec(5), SeekMode::Exact});
    EXPECT_FALSE(engine->snapshot().isPlaying);
    src().setCache(9.0);
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_TRUE(engine->snapshot().isPlaying);
}

TEST_F(EngineSeekTest, StarvationDoesNotBlockSeekCompletion) {
    openReady();
    send(Play{});
    pipeline().setStarved(true);
    sched.runUntilIdle();
    ASSERT_EQ(status(), Status::Buffering);

    send(Seek{sec(5), SeekMode::Exact});
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(rec->count(PlayerEvent::SeekDone), 1);
}

TEST_F(EngineSeekTest, NotSeekableIsRejected) {
    open(10.0, false);
    send(Seek{sec(5), SeekMode::Exact});
    EXPECT_EQ(rec->count(PlayerEvent::OnError), 1);
    EXPECT_TRUE(pipeline().flushes.empty());
    EXPECT_TRUE(src().seeks.empty());
}

TEST_F(EngineSeekTest, SeekWithoutASessionIsRejected) {
    send(Seek{sec(5), SeekMode::Exact});
    EXPECT_EQ(rec->count(PlayerEvent::OnError), 1);
}

TEST_F(EngineSeekTest, SeekAfterFailureIsRejected) {
    open();
    src().fail = Error::StreamInvalidData;
    src().wake();
    sched.runUntilIdle();
    const auto errors = rec->count(PlayerEvent::OnError);
    send(Seek{sec(5), SeekMode::Exact});
    EXPECT_EQ(rec->count(PlayerEvent::OnError), errors + 1);
}

TEST_F(EngineSeekTest, NearTargetCompletesWithoutRepositioning) {
    openReady();
    send(Seek{sec(0.03), SeekMode::Exact});
    EXPECT_EQ(rec->count(PlayerEvent::SeekDone), 1);
    EXPECT_TRUE(pipeline().flushes.empty());
    EXPECT_TRUE(src().seeks.empty());
    EXPECT_EQ(status(), Status::Ready);
}

TEST_F(EngineSeekTest, NearTargetDoesNotShortCutWhileASeekIsInFlight) {
    openReady();
    send(Seek{sec(5), SeekMode::Exact});
    send(Seek{sec(5.03), SeekMode::Exact});
    EXPECT_EQ(pipeline().flushes.size(), 2u);
}

TEST_F(EngineSeekTest, TargetIsClampedToTheDuration) {
    openReady(10.0);
    send(Seek{sec(99), SeekMode::Exact});
    ASSERT_EQ(pipeline().flushes.size(), 1u);
    EXPECT_EQ(pipeline().flushes[0].target, sec(10));
}

TEST_F(EngineSeekTest, SeekWhileOpeningOnlyRecordsTheStartPosition) {
    prepare();
    send(Seek{sec(3), SeekMode::Exact});
    send(Seek{sec(4), SeekMode::Exact});
    EXPECT_EQ(status(), Status::Opening);
    EXPECT_TRUE(src().seeks.empty());

    src().setTracks(10.0);
    sched.runUntilIdle();
    ASSERT_EQ(src().seeks.size(), 1u);
    EXPECT_EQ(src().seeks[0].target, sec(4));
}

TEST_F(EngineSeekTest, PrepareStartPositionSeeksSilently) {
    Prepare p;
    p.startPos = sec(3);
    prepare(p);
    src().setTracks(10.0);
    sched.runUntilIdle();
    ASSERT_EQ(src().seeks.size(), 1u);
    EXPECT_EQ(src().seeks[0].target, sec(3));

    src().setCache(6.0);
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(rec->count(PlayerEvent::SeekDone), 0);
    EXPECT_EQ(status(), Status::Ready);
}

TEST_F(EngineSeekTest, SeekKeepsEpochCounterMonotonicAcrossStop) {
    openReady();
    send(Seek{sec(5), SeekMode::Exact});
    const auto before = pipeline().currentEpoch;
    send(Stop{});
    open();
    EXPECT_GT(pipeline().currentEpoch, before);
}

TEST_F(EngineSeekTest, RetargetWithinTheProducedRangeKeepsTheEpochAndDoesNotReposition) {
    openReady();
    send(Seek{sec(5), SeekMode::Exact});
    const auto epoch = pipeline().currentEpoch;
    src().coversTarget = true;
    send(Seek{sec(6), SeekMode::Fast});

    EXPECT_EQ(src().seeks.size(), 1u);
    EXPECT_EQ(pipeline().flushes.size(), 1u);
    ASSERT_EQ(pipeline().retargets.size(), 1u);
    EXPECT_EQ(pipeline().retargets[0].epoch, epoch);
    EXPECT_EQ(pipeline().retargets[0].target, sec(6));
    EXPECT_EQ(pipeline().retargets[0].mode, SeekMode::Fast);
    EXPECT_EQ(pipeline().currentEpoch, epoch);
}

TEST_F(EngineSeekTest, RetargetedSeekCompletesOnceAndAgainstTheNewTarget) {
    openReady();
    send(Seek{sec(5), SeekMode::Exact});
    src().coversTarget = true;
    send(Seek{sec(6), SeekMode::Exact});
    send(Seek{sec(7), SeekMode::Exact});

    EXPECT_EQ(engine->snapshot().position, sec(7));
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(rec->count(PlayerEvent::SeekDone), 1);
}

TEST_F(EngineSeekTest, SourceDecliningTheRetargetFallsBackToARealSeek) {
    openReady();
    send(Seek{sec(5), SeekMode::Exact});
    src().coversTarget = false;
    send(Seek{sec(2), SeekMode::Exact});

    EXPECT_EQ(src().seeks.size(), 2u);
    EXPECT_EQ(pipeline().flushes.size(), 2u);
    EXPECT_TRUE(pipeline().retargets.empty());
    EXPECT_NE(src().seeks[0].epoch, src().seeks[1].epoch);
}

TEST_F(EngineSeekTest, RetargetIsNotAskedWhenNoSeekIsInFlight) {
    openReady();
    src().coversTarget = true;
    send(Seek{sec(5), SeekMode::Exact});
    EXPECT_TRUE(src().retargets.empty());
    EXPECT_EQ(src().seeks.size(), 1u);
}

TEST_F(EngineSeekTest, RetargetAfterTheSeekCompletedIsARealSeek) {
    openReady();
    send(Seek{sec(5), SeekMode::Exact});
    pipeline().becomeReady();
    sched.runUntilIdle();
    ASSERT_EQ(rec->count(PlayerEvent::SeekDone), 1);

    src().coversTarget = true;
    send(Seek{sec(6), SeekMode::Exact});
    EXPECT_EQ(src().seeks.size(), 2u);
    EXPECT_TRUE(src().retargets.empty());
}

TEST_F(EngineSeekTest, RetargetOfASilentSeekMakesItReportSeekDone) {
    Prepare p;
    p.startPos = sec(3);
    prepare(p);
    src().setTracks(10.0);
    sched.runUntilIdle();
    src().coversTarget = true;
    send(Seek{sec(4), SeekMode::Exact});
    src().setCache(6.0);
    pipeline().becomeReady();
    sched.runUntilIdle();
    EXPECT_EQ(rec->count(PlayerEvent::SeekDone), 1);
}
