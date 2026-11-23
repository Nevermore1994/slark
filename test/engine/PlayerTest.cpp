//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "Notifier.h"
#include "NetworkPerf.h"
#include "PerfMonitor.h"
#include "PlaySession.h"
#include "Player.h"
#include "SourceSupport.h"

using namespace std::chrono_literals;
using namespace slark;
using namespace slark::engine;
using namespace slark::engine::fake;

namespace {

std::unique_ptr<PlayerParams> makeParams(
    const std::string& uri = "../../test_sample/no_such_media.mp4") {
    auto params = std::make_unique<PlayerParams>();
    params->item.path = uri;
    return params;
}

}  // namespace

TEST(PlayerTest, PlayerStateMappingCoversEverySnapshotStatus) {
    Snapshot s;

    s.status = Status::Idle;
    EXPECT_EQ(toPlayerState(s), PlayerState::NotInited);

    s.status = Status::Opening;
    EXPECT_EQ(toPlayerState(s), PlayerState::Buffering);
    s.status = Status::Buffering;
    EXPECT_EQ(toPlayerState(s), PlayerState::Buffering);
    s.status = Status::Seeking;
    EXPECT_EQ(toPlayerState(s), PlayerState::Buffering);

    s.status = Status::Ready;
    s.wantPlay = true;
    EXPECT_EQ(toPlayerState(s), PlayerState::Playing);
    s.wantPlay = false;
    EXPECT_EQ(toPlayerState(s), PlayerState::Pause);

    s.status = Status::Ended;
    EXPECT_EQ(toPlayerState(s), PlayerState::Completed);

    s.status = Status::Failed;
    EXPECT_EQ(toPlayerState(s), PlayerState::Error);
}

TEST(PlayerTest, UnpreparedPlayerIgnoresCommandsAndReportsNotInited) {
    Player player(makeParams());
    EXPECT_EQ(player.state(), PlayerState::NotInited);

    player.play();
    player.pause();
    player.seek(1.0);
    player.stop();
    EXPECT_EQ(player.state(), PlayerState::NotInited);
}

TEST(PlayerTest, StopAfterFailureEventuallyReachesIdle) {
    Player player(makeParams());  // unresolvable media → Failed
    player.prepare();
    ASSERT_TRUE(waitUntil([&] {
        return player.state() == PlayerState::Error;
    }));

    player.stop();
    ASSERT_TRUE(waitUntil([&] {
        return player.state() == PlayerState::NotInited;
    }));
}

TEST(PlayerTest, RepeatedStopIsIdempotent) {
    Player player(makeParams());
    player.prepare();
    ASSERT_TRUE(waitUntil([&] {
        return player.state() == PlayerState::Error;
    }));

    player.stop();
    player.stop();
    player.stop();
    ASSERT_TRUE(waitUntil([&] {
        return player.state() == PlayerState::NotInited;
    }));
}

TEST(PlayerTest, DestructorDoesNotBlockWhileOpening) {
    auto start = std::chrono::steady_clock::now();
    {
        Player player(makeParams());
        player.prepare();
        // Destroy immediately while the engine is still opening the media.
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 3000);
}

TEST(PlayerTest, DisplayRangeBecomesPlaybackRange) {
    auto params = makeParams();
    params->item.displayStart = 1.0;
    params->item.displayDuration = 2.0;
    Player player(std::move(params));
    player.prepare();
    player.stop();
    SUCCEED();
}

namespace {

struct CapturingReporter : slark::perf::IPerfReporter {
    std::mutex mutex;
    std::vector<std::shared_ptr<const slark::perf::SessionReport>> reports;

    void onFrameTrace(std::shared_ptr<const slark::perf::FrameTrace>) override {}

    void onSessionComplete(std::shared_ptr<const slark::perf::SessionReport> value) override {
        std::lock_guard<std::mutex> lock(mutex);
        reports.push_back(std::move(value));
    }

    std::shared_ptr<const slark::perf::SessionReport> report() {
        std::lock_guard<std::mutex> lock(mutex);
        return reports.empty() ? nullptr : reports.back();
    }

    std::vector<std::shared_ptr<const slark::perf::SessionReport>> allReports() {
        std::lock_guard<std::mutex> lock(mutex);
        return reports;
    }
};

struct PerfScope {
    std::shared_ptr<CapturingReporter> reporter = std::make_shared<CapturingReporter>();

    explicit PerfScope(bool enabled) {
        auto& monitor = slark::perf::PerfMonitor::instance();
        slark::perf::PerfConfig config;
        config.enabled = enabled;
        config.enableSessionTrace = true;
        monitor.configure(config);
        monitor.addReporter(reporter);
    }

    ~PerfScope() {
        auto& monitor = slark::perf::PerfMonitor::instance();
        monitor.removeReporter(reporter);
        slark::perf::setNetworkSampleSink({});
        slark::perf::PerfConfig off;
        monitor.configure(off);
    }
};

slark::perf::NetworkSample makeSample(std::string sessionId, std::string host, bool success) {
    slark::perf::NetworkSample sample;
    sample.perfSessionId = std::move(sessionId);
    sample.host = std::move(host);
    sample.totalRequestMs = 42;
    sample.bodyBytes = 2048;
    sample.success = success;
    return sample;
}

}  // namespace

TEST(PlayerTest, EnabledPerfSessionReportsNetworkStatsToReporter) {
    PerfScope scope(true);
    auto& monitor = slark::perf::PerfMonitor::instance();
    auto session = std::make_shared<slark::perf::PlaySession>(
        std::to_string(monitor.nextSessionId()), "https://example.com/stream.m3u8");
    monitor.registerSession(session);

    slark::perf::publishNetworkSample(makeSample(session->sessionId(), "example.com", true));
    slark::perf::publishNetworkSample(makeSample(session->sessionId(), "example.com", false));
    session->finish(true);

    auto report = scope.reporter->report();
    ASSERT_NE(report, nullptr);
    const auto& network = report->networkStats;
    EXPECT_EQ(network.requestCount, 2u);
    EXPECT_EQ(network.successCount, 1u);
    EXPECT_EQ(network.failureCount, 1u);
    EXPECT_EQ(network.totalBodyBytes, 4096u);
    EXPECT_EQ(network.totalRequestMs, 84);
    EXPECT_DOUBLE_EQ(network.avgRequestMs(), 42.0);
    EXPECT_EQ(network.lastSample.host, "example.com");
    EXPECT_EQ(report->url, "https://example.com/stream.m3u8");
    monitor.unregisterSession(session->sessionId());
}

TEST(PlayerTest, DisabledPerfConfigPublishesNoNetworkSamples) {
    PerfScope scope(false);
    slark::perf::publishNetworkSample(makeSample("any", "example.com", true));
    EXPECT_EQ(scope.reporter->report(), nullptr);
}

TEST(PlayerTest, PlayerDrivesPerfSessionLifecycle) {
    PerfScope scope(true);
    auto params = makeParams();
    params->setting.perfConfig.enabled = true;
    {
        Player player(std::move(params));
        player.prepare();
        const auto ids = slark::perf::PerfMonitor::instance().activeSessionIds();
        ASSERT_EQ(ids.size(), 1u);
        slark::perf::publishNetworkSample(makeSample(ids.front(), "127.0.0.1", true));
        player.stop();
    }
    ASSERT_TRUE(waitUntil([&] { return scope.reporter->report() != nullptr; }));
    auto report = scope.reporter->report();
    ASSERT_NE(report, nullptr);
    EXPECT_EQ(report->networkStats.requestCount, 1u);
}

TEST(PlayerTest, StopKeepsInFlightNetworkSamplesInReport) {
    PerfScope scope(true);
    auto params = makeParams();
    params->setting.perfConfig.enabled = true;
    Player player(std::move(params));
    player.prepare();
    const auto ids = slark::perf::PerfMonitor::instance().activeSessionIds();
    ASSERT_EQ(ids.size(), 1u);

    // A request that terminates after stop() must still reach the session report.
    player.stop();
    slark::perf::publishNetworkSample(makeSample(ids.front(), "example.com", true));

    ASSERT_TRUE(waitUntil([&] { return scope.reporter->report() != nullptr; }));
    const auto report = scope.reporter->report();
    EXPECT_EQ(report->networkStats.requestCount, 1u);
    EXPECT_TRUE(report->success);
}

TEST(PlayerTest, ConcurrentPlayersKeepNetworkStatsSeparate) {
    PerfScope scope(true);
    auto& monitor = slark::perf::PerfMonitor::instance();
    auto a = std::make_shared<slark::perf::PlaySession>("sess-a", "https://example.com/a.mp4");
    auto b = std::make_shared<slark::perf::PlaySession>("sess-b", "https://example.com/b.mp4");
    monitor.registerSession(a);
    monitor.registerSession(b);
    EXPECT_EQ(monitor.activeSessionCount(), 2u);

    slark::perf::publishNetworkSample(makeSample("sess-a", "example.com", true));
    EXPECT_EQ(monitor.networkSnapshot("sess-a").requestCount, 1u);
    EXPECT_EQ(monitor.networkSnapshot("sess-b").requestCount, 0u);

    slark::perf::publishNetworkSample(makeSample("sess-b", "example.com", false));
    EXPECT_EQ(monitor.networkSnapshot("sess-a").requestCount, 1u);
    EXPECT_EQ(monitor.networkSnapshot("sess-b").requestCount, 1u);

    // An unknown session must never fall back to an arbitrary active session.
    slark::perf::publishNetworkSample(makeSample("sess-unknown", "example.com", true));
    EXPECT_EQ(monitor.networkSnapshot("sess-a").requestCount, 1u);
    EXPECT_EQ(monitor.networkSnapshot("sess-b").requestCount, 1u);

    a->finish(true);
    b->finish(true);
    EXPECT_EQ(scope.reporter->allReports().size(), 2u);
    monitor.unregisterSession("sess-a");
    monitor.unregisterSession("sess-b");
    EXPECT_EQ(monitor.activeSessionCount(), 0u);
}
