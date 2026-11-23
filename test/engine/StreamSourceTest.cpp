//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <chrono>

#include "ScriptedSource.h"
#include "SourceSupport.h"

using namespace std::chrono_literals;
using namespace slark;
using namespace slark::engine;
using namespace slark::engine::fake;
using F = ScriptedDemuxer::Frame;

namespace {

Time::TimePoint sec(double s) {
    return Time::TimePoint::fromSeconds(s);
}

class StreamSourceTest : public ::testing::Test {
 protected:
    void SetUp() override {
        source = ScriptedSource::create(SourceOptions{.uri = "scripted"}, 1, [this] {
            ++wakes;
        });
        ASSERT_TRUE(waitUntil([&] {
            return source->ready();
        }));
    }

    // The first delivery is consumed by open().
    void open() {
        source->stream->deliver(0, 0);
        ASSERT_TRUE(waitUntil([&] {
            return source->info().tracksKnown;
        }));
    }

    [[nodiscard]] static std::vector<double> ptsOf(const std::vector<AVFrameRefPtr>& frames) {
        std::vector<double> out;
        for (const auto& f : frames) {
            if (!f->isEOS) {
                out.push_back(f->ptsTime());
            }
        }
        return out;
    }

    void drainAll(Drained& out) {
        drainInto(*source, out);
    }

    std::unique_ptr<ScriptedSource> source;
    std::atomic<int> wakes{0};
};

}  // namespace

TEST_F(StreamSourceTest, ChunksFromAStaleStreamGenerationAreDropped) {
    open();
    source->demuxer->queue(16, {{AVFrameType::Video, 1.0, true}});
    source->stream->deliver(16, 0);
    ASSERT_TRUE(waitUntil([&] {
        return source->cacheEnd().second() >= 1.0;
    }));

    source->seekTo(sec(5.0), 2);
    ASSERT_TRUE(waitUntil([&] {
        return !source->stream->seekCalls().empty();
    }));
    const auto streamGeneration = source->stream->seekCalls().back().generation;
    ASSERT_NE(streamGeneration, 0u);

    // Stale chunks land at the new request's offset; only the generation tells them apart.
    const auto pos = source->stream->seekCalls().back().pos;
    source->demuxer->queue(pos, {{AVFrameType::Video, 99.0, true}});
    source->stream->deliver(pos, streamGeneration - 1);
    std::this_thread::sleep_for(50ms);
    source->demuxer->queue(pos, {{AVFrameType::Video, 5.0, true}});
    source->stream->deliver(pos, streamGeneration);

    Drained out;
    ASSERT_TRUE(waitUntil([&] {
        drainAll(out);
        return !out.video.empty() && out.video.back()->seekEpoch == 2;
    }));
    for (const auto pts : ptsOf(out.video)) {
        EXPECT_NE(pts, 99.0);
    }
}

TEST_F(StreamSourceTest, AfterASeekTheFirstDeliveredVideoFrameIsAKeyFrame) {
    open();
    source->seekTo(sec(5.0), 2);
    ASSERT_TRUE(waitUntil([&] {
        return !source->stream->seekCalls().empty();
    }));
    const auto streamGeneration = source->stream->seekCalls().back().generation;
    const auto pos = source->stream->seekCalls().back().pos;

    source->demuxer->queue(pos, {{AVFrameType::Video, 4.0, false},
                                 {AVFrameType::Video, 4.1, false},
                                 {AVFrameType::Video, 5.0, true},
                                 {AVFrameType::Video, 5.1, false}});
    source->stream->deliver(pos, streamGeneration);

    Drained out;
    ASSERT_TRUE(waitUntil([&] {
        drainAll(out);
        return out.video.size() >= 2;
    }));
    ASSERT_EQ(ptsOf(out.video), (std::vector<double>{5.0, 5.1}));
    EXPECT_TRUE(isKey(*out.video.front()));
}

TEST_F(StreamSourceTest, KeyFrameGateDoesNotAffectAudio) {
    open();
    source->seekTo(sec(5.0), 2);
    ASSERT_TRUE(waitUntil([&] {
        return !source->stream->seekCalls().empty();
    }));
    const auto call = source->stream->seekCalls().back();

    source->demuxer->queue(call.pos,
                           {{AVFrameType::Audio, 4.0, false}, {AVFrameType::Video, 4.0, false}});
    source->stream->deliver(call.pos, call.generation);

    Drained out;
    ASSERT_TRUE(waitUntil([&] {
        drainAll(out);
        return !out.audio.empty();
    }));
    EXPECT_TRUE(out.video.empty());
}

TEST_F(StreamSourceTest, AfterARepositionOnlyTheExpectedOffsetIsParsed) {
    open();
    source->seekTo(sec(5.0), 2);
    ASSERT_TRUE(waitUntil([&] {
        return !source->stream->seekCalls().empty();
    }));
    const auto call = source->stream->seekCalls().back();

    source->demuxer->queue(call.pos + 64, {{AVFrameType::Video, 77.0, true}});
    source->stream->deliver(call.pos + 64, call.generation);
    source->demuxer->queue(call.pos, {{AVFrameType::Video, 5.0, true}});
    source->stream->deliver(call.pos, call.generation);

    Drained out;
    ASSERT_TRUE(waitUntil([&] {
        drainAll(out);
        return !out.video.empty();
    }));
    EXPECT_EQ(ptsOf(out.video), (std::vector<double>{5.0}));
}

TEST_F(StreamSourceTest, OnlyTheFirstChunkAfterARepositionIsOffsetChecked) {
    open();
    source->seekTo(sec(5.0), 2);
    ASSERT_TRUE(waitUntil([&] {
        return !source->stream->seekCalls().empty();
    }));
    const auto call = source->stream->seekCalls().back();

    source->demuxer->queue(call.pos, {{AVFrameType::Video, 5.0, true}});
    source->stream->deliver(call.pos, call.generation);
    source->demuxer->queue(call.pos + 16, {{AVFrameType::Video, 5.1, false}});
    source->stream->deliver(call.pos + 16, call.generation);

    Drained out;
    ASSERT_TRUE(waitUntil([&] {
        drainAll(out);
        return out.video.size() == 2;
    }));
}

TEST_F(StreamSourceTest, SeekDiscardsSamplesQueuedBeforeIt) {
    open();
    source->demuxer->queue(16, {{AVFrameType::Video, 1.0, true}, {AVFrameType::Audio, 1.0, false}});
    source->stream->deliver(16, 0);
    ASSERT_TRUE(waitUntil([&] {
        return source->cacheEnd().second() >= 1.0;
    }));

    source->seekTo(sec(20.0), 2);
    ASSERT_TRUE(waitUntil([&] {
        return !source->stream->seekCalls().empty();
    }));

    Drained out;
    drainAll(out);
    EXPECT_TRUE(out.video.empty());
    EXPECT_TRUE(out.audio.empty());
}

TEST_F(StreamSourceTest, SeekUsesTheDemuxerPositionForTheTargetTime) {
    open();
    source->demuxer->seekPos_ = 4242;
    source->seekTo(sec(12.5), 2);
    ASSERT_TRUE(waitUntil([&] {
        return !source->stream->seekCalls().empty();
    }));
    EXPECT_DOUBLE_EQ(source->demuxer->lastSeekTime.load(), 12.5);
    EXPECT_EQ(source->stream->seekCalls().back().pos, 4242u);
}

TEST_F(StreamSourceTest, EachCompletedSeekGetsAFreshStreamGeneration) {
    open();
    source->seekTo(sec(5.0), 2);
    ASSERT_TRUE(waitUntil([&] {
        return source->stream->seekCalls().size() == 1;
    }));
    source->seekTo(sec(6.0), 3);
    ASSERT_TRUE(waitUntil([&] {
        return source->stream->seekCalls().size() == 2;
    }));
    const auto calls = source->stream->seekCalls();
    EXPECT_LT(calls[0].generation, calls[1].generation);
}

TEST_F(StreamSourceTest, ASeekSupersededBeforeItRunsIsSkipped) {
    open();
    source->demuxer->setBlocked(true);
    source->stream->deliver(16, 0);
    // The loader is stuck in parseData, so both seeks queue up behind it.
    source->seekTo(sec(5.0), 2);
    source->seekTo(sec(6.0), 3);
    source->demuxer->setBlocked(false);
    ASSERT_TRUE(waitUntil([&] {
        return !source->stream->seekCalls().empty();
    }));
    std::this_thread::sleep_for(60ms);
    EXPECT_EQ(source->stream->seekCalls().size(), 1u);
    EXPECT_DOUBLE_EQ(source->demuxer->lastSeekTime.load(), 6.0);
}

TEST_F(StreamSourceTest, ALoaderThatFallsBehindClosesTheGateEvenWithoutAWatermark) {
    open();
    ASSERT_TRUE(source->stream->gateIsOpen());

    // No clock was ever given, so the watermark cannot close the gate; only the cap can.
    source->demuxer->setBlocked(true);
    for (int i = 0; i < 4; ++i) {
        source->stream->deliver(16 + static_cast<uint64_t>(i) * 16, 0);
    }
    EXPECT_TRUE(waitUntil([&] {
        return !source->stream->gateIsOpen();
    }));

    source->demuxer->setBlocked(false);
    EXPECT_TRUE(waitUntil([&] {
        return source->stream->gateIsOpen();
    }));
}

TEST_F(StreamSourceTest, TheGateIsReopenedAsTheLoaderCatchesUp) {
    open();
    source->demuxer->setBlocked(true);
    for (int i = 0; i < 4; ++i) {
        source->stream->deliver(16 + static_cast<uint64_t>(i) * 16, 0);
    }
    ASSERT_TRUE(waitUntil([&] {
        return !source->stream->gateIsOpen();
    }));

    const auto opens = source->stream->gateOpens.load();
    source->demuxer->setBlocked(false);
    ASSERT_TRUE(waitUntil([&] {
        return source->stream->gateOpens.load() > opens;
    }));
}

TEST_F(StreamSourceTest, EosAfterASeekCarriesTheNewEpoch) {
    open();
    source->seekTo(sec(5.0), 4);
    ASSERT_TRUE(waitUntil([&] {
        return !source->stream->seekCalls().empty();
    }));
    const auto call = source->stream->seekCalls().back();
    source->demuxer->queue(call.pos,
                           {{AVFrameType::Video, 5.0, true}, {AVFrameType::Audio, 5.0, false}});
    source->stream->deliver(call.pos, call.generation);
    source->stream->complete(call.generation);

    Drained out;
    ASSERT_TRUE(waitUntil([&] {
        drainAll(out);
        return Drained::hasEos(out.video) && Drained::hasEos(out.audio);
    }));
    EXPECT_EQ(out.video.back()->seekEpoch, 4u);
    EXPECT_EQ(out.audio.back()->seekEpoch, 4u);
}

TEST_F(StreamSourceTest, CompletionOfAnOldStreamGenerationDoesNotEndTheNewOne) {
    open();
    source->seekTo(sec(5.0), 2);
    ASSERT_TRUE(waitUntil([&] {
        return !source->stream->seekCalls().empty();
    }));
    const auto call = source->stream->seekCalls().back();

    source->stream->complete(call.generation - 1);
    std::this_thread::sleep_for(80ms);
    EXPECT_TRUE(source->isLoading());

    source->stream->complete(call.generation);
    EXPECT_TRUE(waitUntil([&] {
        return !source->isLoading();
    }));
}

TEST_F(StreamSourceTest, MoovAtTheEndRepositionsTheStreamOnceThenParses) {
    source.reset();
    source = ScriptedSource::create(SourceOptions{.uri = "scripted"}, 1, [] {
    });
    ASSERT_TRUE(waitUntil([&] {
        return source->ready();
    }));
    source->demuxer->repositionTo = 5000;

    source->stream->deliver(0, 0);
    ASSERT_TRUE(waitUntil([&] {
        return !source->stream->seekCalls().empty();
    }));
    EXPECT_EQ(source->stream->seekCalls().front().pos, 5000u);
    EXPECT_FALSE(source->info().tracksKnown);

    source->stream->deliver(5000, source->stream->seekCalls().front().generation);
    ASSERT_TRUE(waitUntil([&] {
        return source->info().tracksKnown;
    }));
    EXPECT_EQ(source->stream->seekCalls().size(), 1u);
}

TEST_F(StreamSourceTest, AfterOpenTheStreamJumpsBackToTheDataStart) {
    source.reset();
    source = ScriptedSource::create(SourceOptions{.uri = "scripted"}, 1, [] {
    });
    ASSERT_TRUE(waitUntil([&] {
        return source->ready();
    }));
    source->demuxer->headerLength = 40;

    source->stream->deliver(0, 0);
    ASSERT_TRUE(waitUntil([&] {
        return !source->stream->seekCalls().empty();
    }));
    EXPECT_EQ(source->stream->seekCalls().front().pos, 40u);
    EXPECT_TRUE(source->info().tracksKnown);
}

TEST_F(StreamSourceTest, CacheEndIsTheMaximumOfBothTracks) {
    open();
    source->demuxer->queue(16, {{AVFrameType::Audio, 2.0, false}, {AVFrameType::Video, 3.5, true}});
    source->stream->deliver(16, 0);
    ASSERT_TRUE(waitUntil([&] {
        return source->cacheEnd().second() >= 3.5;
    }));
    EXPECT_DOUBLE_EQ(source->cacheEnd().second(), 3.5);
}

TEST_F(StreamSourceTest, CacheEndNeverMovesBackwards) {
    open();
    source->demuxer->queue(16, {{AVFrameType::Video, 5.0, true}});
    source->demuxer->queue(32, {{AVFrameType::Video, 4.0, false}});
    source->stream->deliver(16, 0);
    source->stream->deliver(32, 0);
    ASSERT_TRUE(waitUntil([&] {
        return source->cacheEnd().second() >= 5.0;
    }));
    std::this_thread::sleep_for(60ms);
    EXPECT_DOUBLE_EQ(source->cacheEnd().second(), 5.0);
}

TEST_F(StreamSourceTest, DroppingBelowTheLowWatermarkReopensTheGate) {
    // Regression: with the clock frozen (buffering) the level sat below the low watermark and
    // the loader stayed parked because only publishes re-evaluated the gate.
    SourceOptions o{.uri = "scripted"};
    o.flow.source.lowWatermarkSec = 1.0;
    o.flow.source.highWatermarkSec = 5.0;
    source.reset();
    source = ScriptedSource::create(std::move(o), 1, [] {
    });
    ASSERT_TRUE(waitUntil([&] {
        return source->ready();
    }));
    source->stream->deliver(0, 0);
    ASSERT_TRUE(waitUntil([&] {
        return source->info().tracksKnown;
    }));

    source->regulate(sec(0));
    source->demuxer->queue(16, {{AVFrameType::Video, 6.0, true}});
    source->stream->deliver(16, 0);
    ASSERT_TRUE(waitUntil([&] {
        return source->gateClosedCalls.load() == 1;
    }));

    // No new data can arrive while the gate is closed; the clock reaching the low line must be
    // enough to reopen it (this is what a regulator tick does).
    source->regulate(sec(5.5));
    ASSERT_TRUE(waitUntil([&] {
        return source->gateOpenCalls.load() >= 1;
    }));
    ASSERT_TRUE(waitUntil([&] {
        return source->stream->gateIsOpen();
    }));
}

TEST_F(StreamSourceTest, WatermarkClosesAndReopensTheGateOnTheLoader) {
    SourceOptions o{.uri = "scripted"};
    o.flow.source.lowWatermarkSec = 1.0;
    o.flow.source.highWatermarkSec = 5.0;
    source.reset();
    source = ScriptedSource::create(std::move(o), 1, [] {
    });
    ASSERT_TRUE(waitUntil([&] {
        return source->ready();
    }));
    source->stream->deliver(0, 0);
    ASSERT_TRUE(waitUntil([&] {
        return source->info().tracksKnown;
    }));

    source->regulate(sec(0));
    source->demuxer->queue(16, {{AVFrameType::Video, 6.0, true}});
    source->stream->deliver(16, 0);
    ASSERT_TRUE(waitUntil([&] {
        return source->gateClosedCalls.load() == 1;
    }));
    EXPECT_FALSE(source->stream->gateIsOpen());

    source->regulate(sec(5.5));
    ASSERT_TRUE(waitUntil([&] {
        return source->gateOpenCalls.load() >= 1;
    }));
    ASSERT_TRUE(waitUntil([&] {
        return source->stream->gateIsOpen();
    }));
}

TEST_F(StreamSourceTest, NothingTouchesTheStreamAfterClose) {
    open();
    source->close();
    EXPECT_EQ(source->streamCloses.load(), 1);

    source->regulate(sec(1));
    source->regulate(sec(100));
    std::this_thread::sleep_for(40ms);
    EXPECT_EQ(source->gateClosedCalls.load(), 0);
    EXPECT_EQ(source->gateOpenCalls.load(), 0);
}

TEST_F(StreamSourceTest, FailureFromTheDemuxerFactoryIsReported) {
    SourceOptions o{.uri = "scripted"};
    o.demuxerFactory = [] {
        return std::shared_ptr<IDemuxer>();
    };
    auto s = ScriptedSource::create(std::move(o), 1, [] {
    });
    ASSERT_TRUE(waitUntil([&] {
        return s->ready();
    }));
    s->stream->deliver(0, 0);
    ASSERT_TRUE(waitUntil([&] {
        return s->failure().has_value();
    }));
    EXPECT_EQ(s->failure().value(), Error::PipelineFormatNotSupported);
}

class RetargetTest : public StreamSourceTest {
 protected:
    // Seek to 5s and let the loader apply it, then produce up to `producedTo` seconds.
    void seekAndProduce(double producedTo) {
        open();
        source->seekTo(sec(5.0), 2);
        ASSERT_TRUE(waitUntil([&] {
            return !source->stream->seekCalls().empty();
        }));
        const auto call = source->stream->seekCalls().back();
        source->demuxer->queue(
            call.pos, {{AVFrameType::Video, 5.0, true}, {AVFrameType::Video, producedTo, false}});
        source->stream->deliver(call.pos, call.generation);
        ASSERT_TRUE(waitUntil([&] {
            return source->cacheEnd().second() >= producedTo;
        }));
    }
};

TEST_F(RetargetTest, ATargetInsideTheProducedRangeIsAccepted) {
    seekAndProduce(9.0);
    EXPECT_TRUE(source->retarget(sec(7.0)));
    EXPECT_EQ(source->stream->seekCalls().size(), 1u);
}

TEST_F(RetargetTest, ATargetBeforeTheCurrentOneIsDeclined) {
    seekAndProduce(9.0);
    EXPECT_FALSE(source->retarget(sec(3.0)));
}

TEST_F(RetargetTest, ATargetBeyondWhatHasBeenProducedIsDeclined) {
    seekAndProduce(9.0);
    EXPECT_FALSE(source->retarget(sec(12.0)));
}

TEST_F(RetargetTest, ARetargetMovesTheFloorForTheNextOne) {
    seekAndProduce(9.0);
    ASSERT_TRUE(source->retarget(sec(7.0)));
    EXPECT_FALSE(source->retarget(sec(6.0)));
    EXPECT_TRUE(source->retarget(sec(8.0)));
}

TEST_F(RetargetTest, DeclinedWhileTheLoaderHasNotAppliedTheSeekYet) {
    // Plenty of data is already produced, so only the "not applied yet" rule can say no: the
    // target is inside the produced range and not before the current one.
    seekAndProduce(9.0);
    source->demuxer->setBlocked(true);
    source->stream->deliver(source->stream->seekCalls().back().pos + 16,
                            source->stream->seekCalls().back().generation);
    std::this_thread::sleep_for(30ms);

    source->seekTo(sec(5.5), 3);
    EXPECT_FALSE(source->retarget(sec(7.0)));

    source->demuxer->setBlocked(false);
    ASSERT_TRUE(waitUntil([&] {
        return source->stream->seekCalls().size() == 2;
    }));
    EXPECT_TRUE(waitUntil([&] {
        return source->retarget(sec(5.5));
    }));
}

TEST_F(RetargetTest, AfterARetargetTheSourceStillSeeksNormally) {
    seekAndProduce(9.0);
    ASSERT_TRUE(source->retarget(sec(7.0)));
    source->seekTo(sec(2.0), 3);
    ASSERT_TRUE(waitUntil([&] {
        return source->stream->seekCalls().size() == 2;
    }));
    EXPECT_DOUBLE_EQ(source->demuxer->lastSeekTime.load(), 2.0);
}

class GatePullTest : public StreamSourceTest {
 protected:
    // The first delivery is consumed by open(), so only the two that follow reach parseData.
    int openCallsAfterParsing(bool pulls) {
        source->pulls = pulls;
        open();
        const auto before = source->stream->gateOpens.load();
        source->demuxer->setBlocked(true);
        for (int i = 0; i < 2; ++i) {
            source->stream->deliver(16 + static_cast<uint64_t>(i) * 16, 0);
        }
        source->demuxer->setBlocked(false);
        EXPECT_TRUE(waitUntil([&] {
            return source->demuxer->parsed.load() >= 2;
        }));
        std::this_thread::sleep_for(40ms);
        return source->stream->gateOpens.load() - before;
    }
};

TEST_F(GatePullTest, ASourceThatPullsTheGateReopensItAfterEveryChunk) {
    EXPECT_GE(openCallsAfterParsing(true), 2);
}

TEST_F(GatePullTest, AWatermarkDrivenSourceNeverReopensItFromTheChunkPath) {
    EXPECT_EQ(openCallsAfterParsing(false), 0);
}

TEST(PerfRoutingTest, PerfSessionIdReachesOpenParams) {
    SourceOptions options;
    options.uri = "scripted";
    options.perfSessionId = "sess-e2e";
    auto source = ScriptedSource::create(std::move(options), 1, [] {});
    ASSERT_NE(source, nullptr);
    ASSERT_TRUE(waitUntil([&] { return source->ready(); }));
    EXPECT_EQ(source->openedStream()->openedParams().perfSessionId, "sess-e2e");
    source->close();
}
