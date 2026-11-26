//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "HLSSource.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>

#include "MockHlsServer.h"
#include "SourceSupport.h"

using namespace std::chrono_literals;
using namespace slark;
using namespace slark::engine;
using namespace slark::engine::fake;
using slark::test::MockHlsServer;

namespace {

constexpr const char* kTsPath = "../../test_sample/segment-0.ts";
constexpr double kSegmentDurationSec = 6.0;

Time::TimePoint sec(double s) {
    return Time::TimePoint::fromSeconds(s);
}

std::optional<double> firstPtsAtEpoch(const std::vector<AVFrameRefPtr>& frames,
                                      SeekEpoch epoch) {
    const auto it = std::ranges::find_if(frames, [epoch](const auto& frame) {
        return !frame->isEOS && frame->seekEpoch == epoch;
    });
    if (it == frames.end()) {
        return std::nullopt;
    }
    return (*it)->ptsTime();
}

std::string readTsSample() {
    std::ifstream in(kTsPath, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Three 6s segments of real TS. `discontinuityBeforeLast` puts an EXT-X-DISCONTINUITY before
// the third segment, whose payload repeats from the same file and therefore resets PTS.
std::string vodPlaylist(bool discontinuityBeforeLast) {
    std::string playlist = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:6\n";
    for (int i = 0; i < 3; ++i) {
        if (discontinuityBeforeLast && i == 2) {
            playlist += "#EXT-X-DISCONTINUITY\n";
        }
        playlist += "#EXTINF:6.0,\nseg" + std::to_string(i) + ".ts\n";
    }
    playlist += "#EXT-X-ENDLIST\n";
    return playlist;
}

std::string livePlaylist() {
    return "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:6\n"
           "#EXT-X-MEDIA-SEQUENCE:0\n"
           "#EXTINF:6.0,\nseg0.ts\n";
}

class HLSSourceTest : public ::testing::Test {
 protected:
    void SetUp() override {
        tsBody_ = readTsSample();
        ASSERT_GT(tsBody_.size(), 1000000u);
    }

    void serve(bool live = false, bool discontinuity = false) {
        const auto body = live ? livePlaylist() : vodPlaylist(discontinuity);
        server_.setMediaPlaylistFactory([body](int) {
            return body;
        });
        for (int i = 0; i < 3; ++i) {
            server_.setResource("/seg" + std::to_string(i) + ".ts", tsBody_, "video/mp2t");
        }
        ASSERT_TRUE(server_.start());
    }

    std::unique_ptr<HLSSource> open(SourceOptions options = {}) {
        options.uri = server_.masterUrl();
        if (options.demuxer == DemuxerType::Unknown) {
            options.demuxer = DemuxerType::TS;
        }
        return HLSSource::create(std::move(options), 1, [this] {
            ++wakes_;
        });
    }

    bool waitTracks(Source& source) {
        return waitUntil([&] {
            return source.info().tracksKnown || source.failure().has_value();
        });
    }

    void drainWhileEos(Source& source, Drained& out) {
        waitUntil([&] {
            consume(source, out);
            return Drained::hasEos(out.video) && Drained::hasEos(out.audio);
        });
    }

    MockHlsServer server_;
    std::string tsBody_;
    std::atomic<int> wakes_{0};
};

}  // namespace

TEST_F(HLSSourceTest, VodPlaylistReportsDurationSeekableAndTracks) {
    serve();
    auto source = open();
    ASSERT_TRUE(waitTracks(*source));
    EXPECT_FALSE(source->failure());
    EXPECT_TRUE(source->info().isSeekable);
    EXPECT_FALSE(source->info().isLive);
    EXPECT_GT(source->info().duration.second(), 0.0);

    Drained out;
    drainWhileEos(*source, out);
    EXPECT_NEAR(source->info().duration.second(), 3 * kSegmentDurationSec, 0.5);
    EXPECT_GT(out.video.size(), 10u);
    EXPECT_GT(out.audio.size(), 10u);
}

TEST_F(HLSSourceTest, FramesStayMonotonicAcrossDiscontinuity) {
    serve(/*live=*/false, /*discontinuity=*/true);
    auto source = open();
    ASSERT_TRUE(waitTracks(*source));

    Drained out;
    drainWhileEos(*source, out);
    // Decoding order interleaves B-frame PTS by a frame or two; only a segment-boundary
    // reset (whole seconds) must be repaired.
    auto noLargeRewind = [](const std::vector<AVFrameRefPtr>& frames) {
        for (size_t i = 1; i < frames.size(); ++i) {
            if (frames[i]->isEOS) {
                break;
            }
            if (frames[i - 1]->ptsTime() - frames[i]->ptsTime() > 1.0) {
                return false;
            }
        }
        return true;
    };
    EXPECT_TRUE(noLargeRewind(out.audio));
    EXPECT_TRUE(noLargeRewind(out.video));
}

TEST_F(HLSSourceTest, SeekToTimeSwitchesEpochAndDropsStaleFrames) {
    serve();
    auto source = open();
    ASSERT_TRUE(waitTracks(*source));

    Drained first;
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, first);
        return source->cacheEnd() > sec(1.0);
    }));

    source->seekTo(sec(0.0), 2);
    Drained after;
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, after);
        return Drained::hasEos(after.video) && Drained::hasEos(after.audio);
    }));
    // Frames already in flight when the seek landed may still carry epoch 1; everything from
    // the first epoch-2 frame on, including both EOS markers, must be epoch 2.
    auto tailIsNewEpoch = [](const std::vector<AVFrameRefPtr>& frames) {
        size_t firstNew = frames.size();
        for (size_t i = 0; i < frames.size(); ++i) {
            if (frames[i]->seekEpoch == 2u) {
                firstNew = i;
                break;
            }
        }
        if (firstNew == frames.size()) {
            return false;
        }
        for (size_t i = firstNew; i < frames.size(); ++i) {
            if (frames[i]->seekEpoch != 2u) {
                return false;
            }
        }
        return true;
    };
    ASSERT_TRUE(tailIsNewEpoch(after.video));
    ASSERT_TRUE(tailIsNewEpoch(after.audio));
    EXPECT_TRUE(after.video.back()->isEOS);
    EXPECT_GT(after.video.size(), 10u);
}

TEST_F(HLSSourceTest, SeekWithinSegmentKeepsEstablishedPtsOffset) {
    serve();
    auto source = open();
    ASSERT_TRUE(waitTracks(*source));

    Drained before;
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, before);
        return firstPtsAtEpoch(before.video, 1) && firstPtsAtEpoch(before.audio, 1);
    }));
    const auto initialVideoPts = *firstPtsAtEpoch(before.video, 1);
    const auto initialAudioPts = *firstPtsAtEpoch(before.audio, 1);

    source->seekTo(sec(0.5), 2);
    Drained after;
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, after);
        return firstPtsAtEpoch(after.video, 2) && firstPtsAtEpoch(after.audio, 2);
    }));
    EXPECT_NEAR(*firstPtsAtEpoch(after.video, 2), initialVideoPts, 0.05);
    EXPECT_NEAR(*firstPtsAtEpoch(after.audio, 2), initialAudioPts, 0.05);
    EXPECT_LT(*firstPtsAtEpoch(after.video, 2), 0.5);
    EXPECT_LT(*firstPtsAtEpoch(after.audio, 2), 0.5);
}

TEST_F(HLSSourceTest, SeekWithinSegmentBeforeFirstFrameUsesSegmentStart) {
    serve();
    auto source = open();
    source->seekTo(sec(0.5), 2);

    Drained after;
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, after);
        return firstPtsAtEpoch(after.video, 2) && firstPtsAtEpoch(after.audio, 2);
    }));
    EXPECT_GE(*firstPtsAtEpoch(after.video, 2), 0.0);
    EXPECT_GE(*firstPtsAtEpoch(after.audio, 2), 0.0);
    EXPECT_LT(*firstPtsAtEpoch(after.video, 2), 0.5);
    EXPECT_LT(*firstPtsAtEpoch(after.audio, 2), 0.5);
}

TEST_F(HLSSourceTest, SeekToDiscontinuityReanchorsAtSegmentStart) {
    serve(/*live=*/false, /*discontinuity=*/true);
    auto source = open();
    ASSERT_TRUE(waitTracks(*source));

    Drained before;
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, before);
        return firstPtsAtEpoch(before.video, 1) && firstPtsAtEpoch(before.audio, 1);
    }));

    source->seekTo(sec(12.5), 2);
    Drained after;
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, after);
        return firstPtsAtEpoch(after.video, 2) && firstPtsAtEpoch(after.audio, 2);
    }));
    EXPECT_NEAR(*firstPtsAtEpoch(after.video, 2), 12.0, 0.05);
    EXPECT_NEAR(*firstPtsAtEpoch(after.audio, 2), 12.0, 0.05);
}

TEST_F(HLSSourceTest, ClosedWatermarkGateStopsSegmentRequestsUntilTheClockMoves) {
    SourceOptions options;
    options.flow.source.lowWatermarkSec = 1.0;
    options.flow.source.highWatermarkSec = 5.0;
    serve();
    auto source = open(std::move(options));
    ASSERT_TRUE(waitTracks(*source));

    // One 6s segment already exceeds the 5s high watermark, so the next segment request must
    // never appear while the clock stays at zero.
    ASSERT_TRUE(waitUntil([&] {
        return source->cacheEnd() > sec(5.0);
    }));
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(server_.requestCount("/seg1.ts"), 0);

    source->regulate(source->cacheEnd());
    ASSERT_TRUE(waitUntil([&] {
        return server_.requestCount("/seg1.ts") > 0;
    }));
}

TEST_F(HLSSourceTest, LivePlaylistKeepsRefreshingWhileTheWatermarkGateIsClosed) {
    SourceOptions options;
    options.flow.source.lowWatermarkSec = 1.0;
    options.flow.source.highWatermarkSec = 5.0;
    server_.setMediaPlaylistFactory([](int request) {
        std::string playlist =
            "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:0\n";
        for (int i = 0; i < std::min(request, 6); ++i) {
            playlist += "#EXTINF:6.0,\nseg" + std::to_string(i) + ".ts\n";
        }
        return playlist;
    });
    for (int i = 0; i < 6; ++i) {
        server_.setResource("/seg" + std::to_string(i) + ".ts", tsBody_, "video/mp2t");
    }
    ASSERT_TRUE(server_.start());
    auto source = open(std::move(options));
    ASSERT_TRUE(waitTracks(*source));

    double clockSec = 0.0;
    for (int cycle = 0; cycle < 3; ++cycle) {
        ASSERT_TRUE(waitUntil(
            [&] {
                return source->cacheEnd().second() > clockSec + 5.0;
            },
            10s))
            << "cycle " << cycle << ": no segment arrived; playlist requests="
            << server_.requestCount("/media.m3u8");
        std::this_thread::sleep_for(300ms);
        const auto refreshes = server_.requestCount("/media.m3u8");
        ASSERT_TRUE(waitUntil(
            [&] {
                return server_.requestCount("/media.m3u8") > refreshes;
            },
            4s))
            << "cycle " << cycle << ": the playlist stopped refreshing while the gate was closed";
        clockSec = source->cacheEnd().second();
        source->regulate(sec(clockSec));
    }
    EXPECT_GT(server_.requestCount("/seg2.ts"), 0);
}

TEST_F(HLSSourceTest, LivePlaylistReportsLiveAndSeekIsRejected) {
    serve(/*live=*/true);
    auto source = open();
    ASSERT_TRUE(waitUntil([&] {
        return source->info().tracksKnown || source->failure().has_value();
    }));
    ASSERT_TRUE(source->info().tracksKnown);
    EXPECT_TRUE(source->info().isLive);
    EXPECT_FALSE(source->info().isSeekable);

    source->seekTo(sec(1.0), 2);
    ASSERT_TRUE(waitUntil([&] {
        return source->failure().has_value();
    }));
    EXPECT_EQ(source->failure().value(), Error::InvalidOperation);
}

TEST_F(HLSSourceTest, PlaybackRangeRebasesStartAndRaisesEosAtEnd) {
    SourceOptions options;
    options.playbackRange = PlaybackRange{.startSec = 0.5, .endSec = 1.5};
    serve();
    auto source = open(std::move(options));
    ASSERT_TRUE(waitTracks(*source));

    Drained out;
    drainWhileEos(*source, out);
    ASSERT_TRUE(Drained::hasEos(out.video));
    for (const auto& frame : out.video) {
        if (frame->isEOS) {
            continue;
        }
        EXPECT_GE(frame->ptsTime(), 0.0);
        EXPECT_LT(frame->ptsTime(), 1.0);
    }
    const auto first = out.video.front()->ptsTime();
    EXPECT_LT(first, 0.1);
}

TEST_F(HLSSourceTest, LiveStartsWithAKeyVideoFrame) {
    serve(/*live=*/true);
    auto source = open();
    ASSERT_TRUE(waitUntil([&] {
        return source->info().tracksKnown;
    }));

    Drained out;
    ASSERT_TRUE(waitUntil([&] {
        drainInto(*source, out);
        return !out.video.empty();
    }));
    EXPECT_TRUE(isKey(*out.video.front()));
}

TEST_F(HLSSourceTest, PlaybackRangeTrimsReportedDuration) {
    SourceOptions options;
    options.playbackRange = PlaybackRange{.startSec = 1.0, .endSec = 2.0};
    serve();
    auto source = open(std::move(options));
    ASSERT_TRUE(waitTracks(*source));

    Drained out;
    drainWhileEos(*source, out);
    EXPECT_NEAR(source->info().duration.second(), 1.0, 0.2);
}
