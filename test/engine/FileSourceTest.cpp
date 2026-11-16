//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "FileSource.h"

#include <gtest/gtest.h>

#include <fstream>

#include "SourceSupport.h"

using namespace slark;
using namespace slark::engine;
using namespace slark::engine::fake;

namespace {

constexpr const char* kMp4 = "../../test_sample/Sample.mp4";
constexpr const char* kWav = "../../test_sample/sample-3s.wav";

Time::TimePoint sec(double s) {
    return Time::TimePoint::fromSeconds(s);
}

SourceOptions options(const char* uri) {
    SourceOptions o;
    o.uri = uri;
    return o;
}

class FileSourceTest : public ::testing::Test {
 protected:
    std::unique_ptr<FileSource> open(SourceOptions o, SeekEpoch epoch = 1) {
        auto source = FileSource::create(std::move(o), epoch, [this] {
            ++wakes;
        });
        return source;
    }

    bool waitTracks(Source& s) {
        return waitUntil([&] {
            return s.info().tracksKnown || s.failure().has_value();
        });
    }

    bool waitEos(Source& s, Drained& out) {
        return waitUntil([&] {
            consume(s, out);
            return Drained::hasEos(out.video) ||
                   (s.frames(TrackType::Video) == nullptr && Drained::hasEos(out.audio));
        });
    }

    std::atomic<int> wakes{0};
};

}  // namespace

TEST_F(FileSourceTest, Mp4InfoHasBothTracksAndDuration) {
    auto source = open(options(kMp4));
    ASSERT_TRUE(waitTracks(*source));
    const auto info = source->info();
    EXPECT_TRUE(info.tracksKnown);
    EXPECT_TRUE(info.isSeekable);
    EXPECT_FALSE(info.isLive);
    EXPECT_NEAR(info.duration.second(), 64.9, 0.1);
    EXPECT_NE(source->frames(TrackType::Audio), nullptr);
    EXPECT_NE(source->frames(TrackType::Video), nullptr);
    EXPECT_FALSE(source->failure());
}

TEST_F(FileSourceTest, PlaybackRangeTrimsTheReportedDuration) {
    auto trimmed = options(kMp4);
    trimmed.playbackRange = PlaybackRange{.startSec = 10.0, .endSec = 20.0};
    auto source = open(std::move(trimmed));
    ASSERT_TRUE(waitTracks(*source));
    EXPECT_NEAR(source->info().duration.second(), 10.0, 0.001);

    auto full = open(options(kWav));
    ASSERT_TRUE(waitTracks(*full));
    auto beyondEnd = options(kWav);
    beyondEnd.playbackRange = PlaybackRange{.startSec = 1.0, .endSec = 100.0};
    auto wav = open(std::move(beyondEnd));
    ASSERT_TRUE(waitTracks(*wav));
    EXPECT_NEAR(wav->info().duration.second(), full->info().duration.second() - 1.0, 0.001);
}

TEST_F(FileSourceTest, FramesAreNotAvailableBeforeTheTracksAreKnown) {
    auto source = FileSource::create(options("/definitely/not/there.mp4"), 1, [] {
    });
    EXPECT_EQ(source->frames(TrackType::Video), nullptr);
}

TEST_F(FileSourceTest, WavHasAnAudioTrackOnly) {
    auto source = open(options(kWav));
    ASSERT_TRUE(waitTracks(*source));
    EXPECT_NE(source->frames(TrackType::Audio), nullptr);
    EXPECT_EQ(source->frames(TrackType::Video), nullptr);
    EXPECT_NEAR(source->info().duration.second(), 3.2, 0.1);
}

TEST_F(FileSourceTest, ProducedTimestampsAreMonotonicAndFirstVideoIsAKeyFrame) {
    auto source = open(options(kMp4));
    Drained out;
    ASSERT_TRUE(waitEos(*source, out));

    ASSERT_FALSE(out.video.empty());
    EXPECT_TRUE(isKey(*out.video.front()));
    for (const auto* track : {&out.audio, &out.video}) {
        double last = -1;
        for (const auto& f : *track) {
            if (f->isEOS) {
                continue;
            }
            EXPECT_GE(f->ptsTime(), last);
            last = f->ptsTime();
        }
    }
    EXPECT_GT(out.audio.size(), 3000u);
    EXPECT_GT(out.video.size(), 900u);
}

TEST_F(FileSourceTest, EveryFrameCarriesTheEpochItWasProducedUnder) {
    auto source = open(options(kMp4), 7);
    Drained out;
    ASSERT_TRUE(waitEos(*source, out));
    for (const auto& f : out.audio) {
        ASSERT_EQ(f->seekEpoch, 7u);
    }
    for (const auto& f : out.video) {
        ASSERT_EQ(f->seekEpoch, 7u);
    }
}

TEST_F(FileSourceTest, EosIsPostedOncePerTrackAtTheEnd) {
    auto source = open(options(kMp4));
    Drained out;
    ASSERT_TRUE(waitEos(*source, out));
    waitUntil([&] {
        consume(*source, out);
        return Drained::hasEos(out.audio);
    });
    auto eosCount = [](const std::vector<AVFrameRefPtr>& v) {
        return std::count_if(v.begin(), v.end(), [](const auto& f) {
            return f->isEOS;
        });
    };
    EXPECT_EQ(eosCount(out.audio), 1);
    EXPECT_EQ(eosCount(out.video), 1);
    EXPECT_TRUE(out.audio.back()->isEOS);
    EXPECT_TRUE(out.video.back()->isEOS);
    EXPECT_FALSE(source->isLoading());
}

TEST_F(FileSourceTest, CacheEndGrowsAsAbsoluteTimeAndReachesTheDuration) {
    auto source = open(options(kMp4));
    Drained out;
    ASSERT_TRUE(waitEos(*source, out));
    EXPECT_NEAR(source->cacheEnd().second(), 64.8, 0.2);
}

TEST_F(FileSourceTest, ClosedGateStopsProductionAndOvershootIsBounded) {
    auto o = options(kMp4);
    o.flow.source.lowWatermarkSec = 1.0;
    o.flow.source.highWatermarkSec = 5.0;
    auto source = open(std::move(o));
    ASSERT_TRUE(waitTracks(*source));

    const auto pump = [&] {
        source->regulate(sec(0));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return source->cacheEnd().second() >= 5.0;
    };
    ASSERT_TRUE(waitUntil(pump));
    for (int i = 0; i < 20; ++i) {
        source->regulate(sec(0));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto settled = source->cacheEnd().second();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_DOUBLE_EQ(source->cacheEnd().second(), settled);
    EXPECT_TRUE(source->isLoading());

    // Sample.mp4 is about 66 kB/s, so a 64KB chunk is about one second of media. After the
    // watermark is crossed, the chunks already queued for the loader still land.
    EXPECT_LE(settled, 5.0 + 1.0 + 2.0 * 1.2);
}

TEST_F(FileSourceTest, GateReopensAtTheLowWatermark) {
    auto o = options(kMp4);
    o.flow.source.lowWatermarkSec = 1.0;
    o.flow.source.highWatermarkSec = 5.0;
    auto source = open(std::move(o));
    ASSERT_TRUE(waitTracks(*source));
    ASSERT_TRUE(waitUntil([&] {
        source->regulate(sec(0));
        return source->cacheEnd().second() >= 5.0;
    }));
    for (int i = 0; i < 20; ++i) {
        source->regulate(sec(0));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto paused = source->cacheEnd().second();

    EXPECT_TRUE(waitUntil([&] {
        source->regulate(sec(paused - 0.5));
        return source->cacheEnd().second() > paused;
    }));
}

TEST_F(FileSourceTest, DeadBandKeepsTheGateClosed) {
    auto o = options(kMp4);
    o.flow.source.lowWatermarkSec = 1.0;
    o.flow.source.highWatermarkSec = 5.0;
    auto source = open(std::move(o));
    ASSERT_TRUE(waitTracks(*source));
    ASSERT_TRUE(waitUntil([&] {
        source->regulate(sec(0));
        return source->cacheEnd().second() >= 5.0;
    }));
    for (int i = 0; i < 20; ++i) {
        source->regulate(sec(0));
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto paused = source->cacheEnd().second();

    source->regulate(sec(paused - 3.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_DOUBLE_EQ(source->cacheEnd().second(), paused);
}

TEST_F(FileSourceTest, SeekRestartsFromAKeyFrameAndDropsOldEpoch) {
    auto source = open(options(kMp4), 1);
    ASSERT_TRUE(waitTracks(*source));
    Drained before;
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, before);
        return before.video.size() > 100;
    }));

    source->seekTo(sec(30.0), 2);
    Drained after;
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, after);
        for (const auto& f : after.video) {
            if (f->seekEpoch == 2) {
                return true;
            }
        }
        return false;
    }));

    const AVFrame* firstNew = nullptr;
    for (const auto& f : after.video) {
        if (f->seekEpoch == 2) {
            firstNew = f.get();
            break;
        }
    }
    ASSERT_NE(firstNew, nullptr);
    EXPECT_TRUE(isKey(*firstNew));
    EXPECT_LE(firstNew->ptsTime(), 30.0);
    EXPECT_GT(firstNew->ptsTime(), 20.0);

    bool seenNew = false;
    for (const auto& f : after.video) {
        if (f->seekEpoch == 2) {
            seenNew = true;
        } else {
            EXPECT_FALSE(seenNew) << "old epoch frame after a new epoch frame";
        }
    }
}

TEST_F(FileSourceTest, SeekResetsCacheEndToTheTarget) {
    auto source = open(options(kMp4), 1);
    Drained out;
    ASSERT_TRUE(waitEos(*source, out));
    ASSERT_GT(source->cacheEnd().second(), 60.0);

    // A clock at the target lets the watermark gate park the reload; otherwise it reaches EOS.
    source->regulate(sec(10.0));
    source->seekTo(sec(10.0), 2);
    ASSERT_TRUE(waitUntil([&] {
        return source->isLoading();
    }));
    EXPECT_LT(source->cacheEnd().second(), 60.0);
}

TEST_F(FileSourceTest, SeekAfterEosProducesDataAndANewEos) {
    auto source = open(options(kMp4), 1);
    Drained first;
    ASSERT_TRUE(waitEos(*source, first));

    source->seekTo(sec(55.0), 2);
    Drained second;
    ASSERT_TRUE(waitEos(*source, second));
    ASSERT_FALSE(second.video.empty());
    for (const auto& f : second.video) {
        if (!f->isEOS) {
            EXPECT_GE(f->ptsTime(), 40.0);
        }
    }
    EXPECT_EQ(second.video.back()->seekEpoch, 2u);
}

TEST_F(FileSourceTest, WavSeekLandsAtTheRequestedSample) {
    auto source = open(options(kWav), 1);
    ASSERT_TRUE(waitTracks(*source));
    source->seekTo(sec(2.0), 2);
    Drained out;
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, out);
        return std::any_of(out.audio.begin(), out.audio.end(), [](const auto& f) {
            return f->seekEpoch == 2 && !f->isEOS;
        });
    }));
    for (const auto& f : out.audio) {
        if (f->seekEpoch == 2 && !f->isEOS) {
            EXPECT_NEAR(f->ptsTime(), 2.0, 0.05);
            break;
        }
    }
}

TEST_F(FileSourceTest, MissingFileFails) {
    auto source = open(options("/definitely/not/there.mp4"));
    ASSERT_TRUE(waitUntil([&] {
        return source->failure().has_value();
    }));
    EXPECT_EQ(source->failure().value(), Error::FileNotFound);
    EXPECT_FALSE(source->info().tracksKnown);
}

TEST_F(FileSourceTest, GarbageFileNeverBecomesPlayable) {
    const std::string path = "garbage.bin";
    {
        std::ofstream f(path, std::ios::binary);
        for (int i = 0; i < 100000; ++i) {
            f.put(static_cast<char>((i * 31) & 0xFF));
        }
    }
    auto source = open(options(path.c_str()));
    EXPECT_TRUE(waitUntil([&] {
        return source->failure().has_value();
    }));
    EXPECT_FALSE(source->info().tracksKnown);
    std::remove(path.c_str());
}

TEST_F(FileSourceTest, CloseIsIdempotentAndDoesNotDeadlock) {
    auto source = open(options(kMp4));
    ASSERT_TRUE(waitTracks(*source));
    source->close();
    source->close();
}

TEST_F(FileSourceTest, DestroyingWhileLoadingDoesNotHang) {
    auto source = open(options(kMp4));
    source.reset();
}

TEST_F(FileSourceTest, DestroyingRightAfterCreateDoesNotHang) {
    for (int i = 0; i < 20; ++i) {
        auto source = open(options(kMp4));
    }
}

TEST_F(FileSourceTest, SeekAfterCloseIsIgnored) {
    auto source = open(options(kMp4));
    ASSERT_TRUE(waitTracks(*source));
    source->close();
    source->seekTo(sec(5), 2);
}

TEST_F(FileSourceTest, ProductionWakesTheEngineAfterTheStateIsVisible) {
    std::atomic<Source*> self{nullptr};
    std::atomic<bool> sawTracksOnWake{false};
    std::atomic<int> count{0};
    auto source = FileSource::create(options(kMp4), 1, [&] {
        auto* s = self.load();
        if (s && s->info().tracksKnown) {
            sawTracksOnWake = true;
        }
        ++count;
    });
    self = source.get();
    ASSERT_TRUE(waitUntil([&] {
        return sawTracksOnWake.load();
    }));
    EXPECT_GT(count.load(), 0);
    self = nullptr;
}

TEST_F(FileSourceTest, ItPullsTheGateAtEveryChunk) {
    auto source = open(options(kMp4));
    EXPECT_TRUE(source->pullsGateForTest());
}
