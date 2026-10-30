//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include "EngineState.h"

using namespace slark::engine;

namespace {

// Priority list restating deriveStatus(); a different shape from the if-chain so a slip in either shows.
Status expectedStatus(bool failure,
                      bool hasSession,
                      bool tracksKnown,
                      bool hasSeekTarget,
                      bool blockingNotReady,
                      bool aheadBelowStart,
                      bool allEnded) {
    struct Rule {
        bool hit;
        Status status;
    };

    const Rule rules[] = {
        {failure, Status::Failed},
        {!hasSession, Status::Idle},
        {!tracksKnown, Status::Opening},
        {hasSeekTarget, Status::Seeking},
        {blockingNotReady || aheadBelowStart, Status::Buffering},
        {allEnded, Status::Ended},
    };
    for (const auto& r : rules) {
        if (r.hit) {
            return r.status;
        }
    }
    return Status::Ready;
}

EngineState makeState(bool failure, bool hasSession, bool tracksKnown, bool hasSeekTarget) {
    EngineState f;
    f.hasSession = hasSession;
    f.info.tracksKnown = tracksKnown;
    if (hasSeekTarget) {
        f.seekTarget = slark::Time::TimePoint(1'000'000);
    }
    if (failure) {
        f.failure = slark::Error::PipelineDecodeError;
    }
    return f;
}

}  // namespace

TEST(DeriveTest, TruthTableIsExhaustive) {
    int checked = 0;
    for (unsigned bits = 0; bits < (1u << 7); ++bits) {
        const bool failure = bits & 1u;
        const bool hasSession = bits & 2u;
        const bool tracksKnown = bits & 4u;
        const bool hasSeekTarget = bits & 8u;
        const bool blockingNotReady = bits & 16u;
        const bool aheadBelowStart = bits & 32u;
        const bool allEnded = bits & 64u;

        const EngineState f = makeState(failure, hasSession, tracksKnown, hasSeekTarget);
        const PipelineView pipelines{blockingNotReady, allEnded, aheadBelowStart};

        EXPECT_EQ(deriveStatus(f, pipelines), expectedStatus(failure, hasSession, tracksKnown, hasSeekTarget,
                                                   blockingNotReady, aheadBelowStart, allEnded))
            << "failure=" << failure << " session=" << hasSession << " tracks=" << tracksKnown
            << " seek=" << hasSeekTarget << " blockingNotReady=" << blockingNotReady
            << " aheadBelowStart=" << aheadBelowStart << " allEnded=" << allEnded;
        ++checked;
    }
    EXPECT_EQ(checked, 128);
}

TEST(DeriveTest, DefaultFactsAreIdle) {
    EXPECT_EQ(deriveStatus(EngineState{}, PipelineView{}), Status::Idle);
}

TEST(DeriveTest, FailureBeatsEverything) {
    EngineState f = makeState(true, true, true, true);
    EXPECT_EQ(deriveStatus(f, PipelineView{true, true, true}), Status::Failed);
    f.hasSession = false;
    EXPECT_EQ(deriveStatus(f, PipelineView{}), Status::Failed);
}

TEST(DeriveTest, OpeningIgnoresSeekTarget) {
    const EngineState f = makeState(false, true, false, true);
    EXPECT_EQ(deriveStatus(f, PipelineView{}), Status::Opening);
}

TEST(DeriveTest, SeekingBeatsBufferingAndEnded) {
    const EngineState f = makeState(false, true, true, true);
    EXPECT_EQ(deriveStatus(f, PipelineView{true, true, true}), Status::Seeking);
}

TEST(DeriveTest, BufferingWhenABlockingPipelineIsNotReady) {
    const EngineState f = makeState(false, true, true, false);
    EXPECT_EQ(deriveStatus(f, PipelineView{true, false, false}), Status::Buffering);
}

TEST(DeriveTest, BufferingWhenAheadIsBelowStartThreshold) {
    const EngineState f = makeState(false, true, true, false);
    EXPECT_EQ(deriveStatus(f, PipelineView{false, false, true}), Status::Buffering);
}

TEST(DeriveTest, BufferingBeatsEnded) {
    const EngineState f = makeState(false, true, true, false);
    EXPECT_EQ(deriveStatus(f, PipelineView{true, true, false}), Status::Buffering);
}

TEST(DeriveTest, EndedWhenEverythingFinished) {
    const EngineState f = makeState(false, true, true, false);
    EXPECT_EQ(deriveStatus(f, PipelineView{false, true, false}), Status::Ended);
}

TEST(DeriveTest, ReadyOtherwise) {
    const EngineState f = makeState(false, true, true, false);
    EXPECT_EQ(deriveStatus(f, PipelineView{false, false, false}), Status::Ready);
}

TEST(DeriveTest, WantPlayDoesNotAffectStatus) {
    EngineState f = makeState(false, true, true, false);
    const PipelineView pipelines{false, false, false};
    f.wantPlay = false;
    const auto paused = deriveStatus(f, pipelines);
    f.wantPlay = true;
    EXPECT_EQ(deriveStatus(f, pipelines), paused);
    EXPECT_EQ(paused, Status::Ready);
}

TEST(DeriveTest, IsPlayingNeedsWantPlayAndReady) {
    EngineState f = makeState(false, true, true, false);
    f.wantPlay = true;
    EXPECT_TRUE(isPlaying(f, Status::Ready));
    EXPECT_FALSE(isPlaying(f, Status::Buffering));
    EXPECT_FALSE(isPlaying(f, Status::Seeking));
    EXPECT_FALSE(isPlaying(f, Status::Opening));
    EXPECT_FALSE(isPlaying(f, Status::Ended));
    EXPECT_FALSE(isPlaying(f, Status::Failed));
    EXPECT_FALSE(isPlaying(f, Status::Idle));

    f.wantPlay = false;
    EXPECT_FALSE(isPlaying(f, Status::Ready));
}

TEST(DeriveTest, DeriveIsPure) {
    const EngineState f = makeState(false, true, true, false);
    const PipelineView pipelines{false, false, false};
    const auto first = deriveStatus(f, pipelines);
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(deriveStatus(f, pipelines), first);
    }
}
