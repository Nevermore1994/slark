//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "HttpMp4Source.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <iterator>

#include "MockHttpServer.h"
#include "SourceSupport.h"
#include "StringUtil.h"

using namespace std::chrono_literals;
using namespace slark;
using namespace slark::engine;
using namespace slark::engine::fake;
using slark::test::MockHttpServer;

namespace {

constexpr const char* kMp4 = "../../test_sample/Sample.mp4";
// Sample.mp4 keeps its moov after the mdat: ftyp 0, free 32, mdat 40..4270607, moov 4270608.
constexpr uint64_t kMoovOffset = 4270608;
constexpr uint64_t kMdatOffset = 40;

Time::TimePoint sec(double s) {
    return Time::TimePoint::fromSeconds(s);
}

std::string readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The same file with moov moved in front of mdat. The mdat payload is untouched, so the sample
// offsets inside moov would be wrong; the demuxer only needs to find the boxes in order here.
std::string moovFirst(const std::string& file) {
    std::string out = file.substr(0, kMdatOffset);
    const auto moov = file.substr(kMoovOffset);
    const auto mdat = file.substr(kMdatOffset, kMoovOffset - kMdatOffset);
    return out + moov + mdat;
}

class HttpMp4SourceTest : public ::testing::Test {
 protected:
    void SetUp() override {
        file = readFile(kMp4);
        ASSERT_EQ(file.size(), 4298693u);
        ASSERT_EQ(file.substr(kMoovOffset + 4, 4), "moov");
    }

    // chunkDelayMs > 0 makes the transfer last long enough for the watermark to act on it; a
    // loopback server otherwise delivers the whole file before any decision can be made.
    void serve(std::string body = {}, bool honorRange = true, int32_t chunkDelayMs = 0) {
        server.setBody(body.empty() ? file : std::move(body));
        ASSERT_TRUE(server.start(honorRange, chunkDelayMs));
    }

    std::unique_ptr<HttpMp4Source> open(SourceOptions o = {}) {
        o.uri = server.url();
        return HttpMp4Source::create(std::move(o), 1, [] {
        });
    }

    bool waitTracks(Source& s) {
        return waitUntil([&] {
            return s.info().tracksKnown || s.failure().has_value();
        });
    }

    std::string file;
    MockHttpServer server;
};

}  // namespace

TEST_F(HttpMp4SourceTest, MoovAtTheEndIsFoundWithOneExtraRangeRequest) {
    serve();
    auto source = open();
    ASSERT_TRUE(waitTracks(*source));
    ASSERT_TRUE(source->info().tracksKnown);
    EXPECT_NEAR(source->info().duration.second(), 64.9, 0.1);
    EXPECT_FALSE(source->failure());

    const auto ranges = server.rangeRequests();
    ASSERT_GE(ranges.size(), 2u);
    EXPECT_EQ(ranges[0], "bytes=0");
    EXPECT_EQ(ranges[1], "bytes=" + std::to_string(kMoovOffset));
}

TEST_F(HttpMp4SourceTest, AfterTheHeaderTheStreamJumpsBackToTheMediaData) {
    serve();
    auto source = open();
    ASSERT_TRUE(waitTracks(*source));

    Drained out;
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, out);
        return Drained::hasEos(out.video);
    }));
    const auto ranges = server.rangeRequests();
    ASSERT_GE(ranges.size(), 3u);
    EXPECT_EQ(ranges[2], "bytes=" + std::to_string(kMdatOffset));
}

TEST_F(HttpMp4SourceTest, ProducesTheWholeFileAndEndsOnce) {
    serve();
    auto source = open();
    Drained out;
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, out);
        return Drained::hasEos(out.video) && Drained::hasEos(out.audio);
    }));
    EXPECT_GT(out.audio.size(), 3000u);
    EXPECT_GT(out.video.size(), 900u);
    EXPECT_TRUE(isKey(*out.video.front()));
    auto eos = [](const std::vector<AVFrameRefPtr>& v) {
        return std::count_if(v.begin(), v.end(), [](const auto& f) {
            return f->isEOS;
        });
    };
    EXPECT_EQ(eos(out.audio), 1);
    EXPECT_EQ(eos(out.video), 1);
    EXPECT_FALSE(source->isLoading());
}

TEST_F(HttpMp4SourceTest, MoovAtTheFrontNeedsNoRepositioning) {
    serve(moovFirst(file));
    auto source = open();
    ASSERT_TRUE(waitTracks(*source));
    ASSERT_TRUE(source->info().tracksKnown);
    for (const auto& r : server.rangeRequests()) {
        EXPECT_NE(r, "bytes=" + std::to_string(kMoovOffset));
    }
}

TEST_F(HttpMp4SourceTest, SeekIssuesANewRangeAndDropsEarlierEpochData) {
    serve();
    auto source = open();
    Drained out;
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, out);
        return out.video.size() > 50;
    }));

    source->seekTo(sec(40.0), 2);
    ASSERT_TRUE(waitUntil([&] {
        consume(*source, out);
        return std::any_of(out.video.begin(), out.video.end(), [](const auto& f) {
            return f->seekEpoch == 2;
        });
    }));

    bool seenNew = false;
    for (const auto& f : out.video) {
        if (f->seekEpoch == 2) {
            if (!seenNew) {
                EXPECT_TRUE(isKey(*f));
                EXPECT_LE(f->ptsTime(), 40.0);
                EXPECT_GT(f->ptsTime(), 30.0);
            }
            seenNew = true;
        } else {
            EXPECT_FALSE(seenNew);
        }
    }
    const auto ranges = server.rangeRequests();
    EXPECT_GT(ranges.back().size(), std::string("bytes=").size());
}

TEST_F(HttpMp4SourceTest, ServerIgnoringRangeCannotBeSoughtButStillFailsCleanly) {
    serve({}, false);
    auto source = open();
    ASSERT_TRUE(waitUntil([&] {
        return source->failure().has_value() || source->info().tracksKnown;
    }));
    // The moov is at the end and the server cannot reposition, so there is nothing to play.
    EXPECT_TRUE(source->failure().has_value());
    EXPECT_FALSE(source->info().tracksKnown);
}

TEST_F(HttpMp4SourceTest, ServerIgnoringRangeStillPlaysAFileWithMoovAtTheFront) {
    serve(moovFirst(file), false);
    auto source = open();
    ASSERT_TRUE(waitTracks(*source));
    EXPECT_TRUE(source->info().tracksKnown);
    EXPECT_FALSE(source->info().isSeekable);
}

TEST_F(HttpMp4SourceTest, RangeCapableServerMakesTheSourceSeekable) {
    serve(moovFirst(file), true);
    auto source = open();
    ASSERT_TRUE(waitTracks(*source));
    EXPECT_TRUE(source->info().isSeekable);
}

TEST_F(HttpMp4SourceTest, NotFoundIsAFailureNotMediaData) {
    server.failWith("404 Not Found");
    ASSERT_TRUE(server.start());
    auto source = open();
    ASSERT_TRUE(waitUntil([&] {
        return source->failure().has_value();
    }));
    EXPECT_EQ(source->failure().value(), Error::HttpError);
    EXPECT_FALSE(source->info().tracksKnown);
    Drained out;
    drainInto(*source, out);
    EXPECT_TRUE(out.video.empty());
}

TEST_F(HttpMp4SourceTest, FailureDetailCarriesTheStatusCodeAndTheHost) {
    server.failWith("404 Not Found");
    ASSERT_TRUE(server.start());
    auto source = open();
    ASSERT_TRUE(waitUntil([&] {
        return source->failure().has_value();
    }));
    const auto detail = source->failureDetail();
    EXPECT_EQ(detail.origin, 404);
    EXPECT_FALSE(detail.host.empty());
    EXPECT_EQ(detail.host, StringUtil::extractHostFromUri(server.url()));
}

TEST_F(HttpMp4SourceTest, ServerErrorIsAFailure) {
    server.failWith("503 Service Unavailable");
    ASSERT_TRUE(server.start());
    auto source = open();
    ASSERT_TRUE(waitUntil([&] {
        return source->failure().has_value();
    }));
    EXPECT_EQ(source->failure().value(), Error::HttpError);
}

TEST_F(HttpMp4SourceTest, HighWatermarkCancelsTheRequestAndLowWatermarkResumesWithRange) {
    serve({}, true, 2);
    SourceOptions o;
    o.flow.source.lowWatermarkSec = 1.0;
    o.flow.source.highWatermarkSec = 4.0;
    auto source = open(std::move(o));
    ASSERT_TRUE(waitTracks(*source));
    ASSERT_TRUE(waitUntil([&] {
        source->regulate(sec(0));
        return source->cacheEnd().second() >= 4.0;
    }));

    for (int i = 0; i < 30; ++i) {
        source->regulate(sec(0));
        std::this_thread::sleep_for(10ms);
    }
    const auto paused = source->cacheEnd().second();
    const auto requestsWhilePaused = server.requestCount();
    ASSERT_TRUE(source->isLoading());
    std::this_thread::sleep_for(300ms);
    EXPECT_DOUBLE_EQ(source->cacheEnd().second(), paused);
    EXPECT_EQ(server.requestCount(), requestsWhilePaused);

    EXPECT_TRUE(waitUntil([&] {
        source->regulate(sec(paused - 0.5));
        return source->cacheEnd().second() > paused;
    }));
    EXPECT_GT(server.requestCount(), requestsWhilePaused);
    EXPECT_NE(server.rangeRequests().back(), "");
}

TEST_F(HttpMp4SourceTest, TheResumedRequestStartsWhereTheLastOneStopped) {
    serve({}, true, 2);
    SourceOptions o;
    o.flow.source.lowWatermarkSec = 1.0;
    o.flow.source.highWatermarkSec = 4.0;
    auto source = open(std::move(o));
    ASSERT_TRUE(waitTracks(*source));
    Drained out;
    ASSERT_TRUE(waitUntil([&] {
        source->regulate(sec(0));
        drainInto(*source, out);
        return source->cacheEnd().second() >= 4.0;
    }));
    for (int i = 0; i < 30; ++i) {
        source->regulate(sec(0));
        std::this_thread::sleep_for(10ms);
    }
    const auto before = server.requestCount();
    ASSERT_TRUE(waitUntil([&] {
        source->regulate(sec(source->cacheEnd().second() - 0.5));
        drainInto(*source, out);
        return server.requestCount() > before;
    }));

    // Nothing may be lost or repeated across the cancel: timestamps stay ordered.
    waitUntil([&] {
        source->regulate(sec(source->cacheEnd().second() - 0.5));
        drainInto(*source, out);
        return out.video.size() > 150;
    });
    double last = -1;
    for (const auto& f : out.video) {
        if (!f->isEOS) {
            EXPECT_GE(f->ptsTime(), last);
            last = f->ptsTime();
        }
    }
}

TEST_F(HttpMp4SourceTest, WatermarkOvershootIsBounded) {
    serve({}, true, 2);
    SourceOptions o;
    o.flow.source.lowWatermarkSec = 1.0;
    o.flow.source.highWatermarkSec = 4.0;
    auto source = open(std::move(o));
    ASSERT_TRUE(waitTracks(*source));
    for (int i = 0; i < 80; ++i) {
        source->regulate(sec(0));
        std::this_thread::sleep_for(5ms);
    }
    // The request is cancelled at the watermark; only what was already on the wire lands.
    EXPECT_LE(source->cacheEnd().second(), 4.0 + 6.0);
    EXPECT_TRUE(source->isLoading());
}

TEST_F(HttpMp4SourceTest, DestroyingWhileDownloadingDoesNotHang) {
    serve();
    for (int i = 0; i < 5; ++i) {
        auto source = open();
        std::this_thread::sleep_for(15ms);
    }
}

TEST_F(HttpMp4SourceTest, UnreachableHostFails) {
    SourceOptions o;
    o.uri = "http://127.0.0.1:1/none.mp4";
    auto source = HttpMp4Source::create(std::move(o), 1, [] {
    });
    ASSERT_TRUE(waitUntil([&] {
        return source->failure().has_value();
    }));
    EXPECT_FALSE(source->info().tracksKnown);
}

TEST_F(HttpMp4SourceTest, ABodyBeyondTheDefaultSizeCapIsNotRejected) {
    constexpr uint64_t kAnnounced = 80ULL * 1024 * 1024;
    slark::test::StallingHttpServer stalling;
    ASSERT_TRUE(stalling.start("200 OK", kAnnounced, 128 * 1024));

    SourceOptions o;
    o.uri = stalling.url();
    auto source = HttpMp4Source::create(std::move(o), 1, [] {
    });
    std::this_thread::sleep_for(400ms);
    // The data is not an MP4, so it never becomes playable, but it must not be refused for its
    // announced size: that would surface as HttpResponseBodyTooLarge.
    if (source->failure()) {
        EXPECT_NE(source->failure().value(), Error::HttpResponseBodyTooLarge);
    }
}

TEST_F(HttpMp4SourceTest, ASlowTransferIsNotCutOffByATotalDeadline) {
    serve({}, true, 3);
    auto source = open();
    ASSERT_TRUE(waitTracks(*source));
    EXPECT_TRUE(source->info().tracksKnown);
    EXPECT_FALSE(source->failure());
}

TEST_F(HttpMp4SourceTest, ItIsDrivenByTheWatermarkNotPulledPerChunk) {
    serve();
    auto source = open();
    EXPECT_FALSE(source->pullsGateForTest());
}
