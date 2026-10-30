//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Notifier.h"

#include <gtest/gtest.h>

#include <deque>

#include "Fakes.h"

using namespace slark;
using namespace slark::engine;
using slark::engine::fake::Recorder;

namespace {

struct QueueExecutor final : IExecutor {
    void execute(MoveOnlyFunction<void()> task) noexcept override {
        tasks.push_back(std::move(task));
    }

    void drain() {
        while (!tasks.empty()) {
            auto task = std::move(tasks.front());
            tasks.pop_front();
            task();
        }
    }

    std::deque<MoveOnlyFunction<void()>> tasks;
};

Time::TimePoint secs(double s) {
    return Time::TimePoint::fromSeconds(s);
}

Time::TimePoint atMs(int64_t ms) {
    return Time::TimePoint(static_cast<uint64_t>(1'000'000 + ms * 1000));
}

void publish(Notifier& notifier, const Snapshot& s) {
    static uint64_t now = 0;
    now += 1'000'000;
    notifier.publish(s, Time::TimePoint(now));
}

Snapshot snap(Status status, double positionSec = 0, bool wantPlay = false,
              bool tracksKnown = false) {
    Snapshot s;
    s.status = status;
    s.wantPlay = wantPlay;
    s.tracksKnown = tracksKnown;
    s.position = Time::TimePoint::fromSeconds(positionSec);
    s.duration = Time::TimePoint::fromSeconds(10.0);
    return s;
}

class NotifierTest : public ::testing::Test {
 protected:
    Notifier notifier{"p1"};
    std::shared_ptr<Recorder> rec = std::make_shared<Recorder>();

    void SetUp() override {
        notifier.addObserver(rec);
    }
};

}  // namespace

TEST_F(NotifierTest, StateIsReportedOncePerChange) {
    publish(notifier, snap(Status::Ready, 0, true));
    publish(notifier, snap(Status::Ready, 0, true));
    publish(notifier, snap(Status::Ready, 0, false));
    EXPECT_EQ(rec->states, (std::vector<PlayerState>{PlayerState::Playing, PlayerState::Pause}));
}

TEST_F(NotifierTest, PreparedIsReportedOnceWhenTracksBecomeKnown) {
    // Prepared must precede the first Ready: the platform starts its vsync loop on it, and
    // the first video frame (which ends Buffering) can only be presented from that loop.
    publish(notifier, snap(Status::Buffering));
    publish(notifier, snap(Status::Buffering, 0, false, /*tracksKnown=*/true));
    publish(notifier, snap(Status::Ready));
    publish(notifier, snap(Status::Ready));
    EXPECT_EQ(rec->states,
              (std::vector<PlayerState>{PlayerState::Buffering, PlayerState::Prepared,
                                        PlayerState::Buffering, PlayerState::Pause}));
}

TEST_F(NotifierTest, IdleResetsTheOneShotPrepared) {
    publish(notifier, snap(Status::Ready, 0, false, /*tracksKnown=*/true));
    publish(notifier, snap(Status::Idle));
    publish(notifier, snap(Status::Ready, 0, false, /*tracksKnown=*/true));
    EXPECT_EQ(rec->states,
              (std::vector<PlayerState>{PlayerState::Prepared, PlayerState::Pause,
                                        PlayerState::NotInited, PlayerState::Prepared,
                                        PlayerState::Pause}));
}

TEST_F(NotifierTest, OpeningBufferingSeekingShareOneState) {
    publish(notifier, snap(Status::Opening));
    publish(notifier, snap(Status::Buffering));
    publish(notifier, snap(Status::Seeking));
    EXPECT_EQ(rec->states, (std::vector<PlayerState>{PlayerState::Buffering}));
}

TEST_F(NotifierTest, InitialIdleIsNotReported) {
    publish(notifier, snap(Status::Idle));
    EXPECT_TRUE(rec->states.empty());
}

TEST_F(NotifierTest, EndedAndFailedMapToCompletedAndError) {
    publish(notifier, snap(Status::Ended));
    publish(notifier, snap(Status::Failed));
    EXPECT_EQ(rec->states, (std::vector<PlayerState>{PlayerState::Completed, PlayerState::Error}));
}

TEST_F(NotifierTest, PositionStepBoundary) {
    publish(notifier, snap(Status::Ready, 1.0));
    publish(notifier, snap(Status::Ready, 1.099));
    EXPECT_EQ(rec->times.size(), 1u);
    publish(notifier, snap(Status::Ready, 1.101));
    EXPECT_EQ(rec->times.size(), 2u);
    EXPECT_DOUBLE_EQ(rec->times.back(), 1.101);
}

TEST_F(NotifierTest, PositionMovingBackwardsAlsoReports) {
    publish(notifier, snap(Status::Ready, 5.0));
    publish(notifier, snap(Status::Ready, 2.0));
    EXPECT_EQ(rec->times.size(), 2u);
}

TEST_F(NotifierTest, EndedReportsDurationOnce) {
    publish(notifier, snap(Status::Ready, 9.95));
    publish(notifier, snap(Status::Ended, 9.97));
    publish(notifier, snap(Status::Ended, 9.97));
    ASSERT_EQ(rec->times.size(), 2u);
    EXPECT_DOUBLE_EQ(rec->times.back(), 10.0);
}

TEST_F(NotifierTest, CacheTimeIsReportedAtMostOncePerInterval) {
    auto s = snap(Status::Ready);
    s.buffered = secs(3.0);
    notifier.publish(s, atMs(0));
    s.buffered = secs(3.4);
    notifier.publish(s, atMs(100));
    s.buffered = secs(3.9);
    notifier.publish(s, atMs(499));
    ASSERT_EQ(rec->events.size(), 1u);
    EXPECT_EQ(rec->events[0].first, PlayerEvent::UpdateCacheTime);
    EXPECT_EQ(rec->events[0].second, std::to_string(3.0));

    notifier.publish(s, atMs(500));
    ASSERT_EQ(rec->events.size(), 2u);
    EXPECT_EQ(rec->events[1].second, std::to_string(3.9));
}

TEST_F(NotifierTest, ThrottledCacheChangeStaysDueUntilItIsDeliveredOrRevoked) {
    auto s = snap(Status::Ready);
    s.buffered = secs(1.0);
    notifier.publish(s, atMs(0));
    EXPECT_FALSE(notifier.cacheReportDue());

    s.buffered = secs(1.05);
    notifier.publish(s, atMs(200));
    ASSERT_TRUE(notifier.cacheReportDue());
    EXPECT_EQ(*notifier.cacheReportDue(), atMs(500));

    s.buffered = secs(1.0);
    notifier.publish(s, atMs(300));
    EXPECT_FALSE(notifier.cacheReportDue());
}

TEST_F(NotifierTest, FinalCacheValueIsDeliveredEvenWhenItMovedLessThanTheProgressStep) {
    auto s = snap(Status::Ready);
    s.buffered = secs(9.93);
    notifier.publish(s, atMs(0));
    s.buffered = secs(10.0);
    notifier.publish(s, atMs(50));
    ASSERT_EQ(rec->events.size(), 1u);
    ASSERT_TRUE(notifier.cacheReportDue());

    notifier.publish(s, *notifier.cacheReportDue());
    ASSERT_EQ(rec->events.size(), 2u);
    EXPECT_EQ(rec->events[1].second, std::to_string(10.0));
    EXPECT_FALSE(notifier.cacheReportDue());
}

TEST_F(NotifierTest, IdleResetsTheCacheThrottle) {
    auto s = snap(Status::Ready);
    s.buffered = secs(2.0);
    notifier.publish(s, atMs(0));
    s.buffered = secs(2.5);
    notifier.publish(s, atMs(10));
    ASSERT_TRUE(notifier.cacheReportDue());

    notifier.publish(snap(Status::Idle), atMs(20));
    EXPECT_FALSE(notifier.cacheReportDue());
    s.buffered = secs(1.0);
    notifier.publish(s, atMs(30));
    ASSERT_EQ(rec->events.size(), 2u);
    EXPECT_EQ(rec->events[1].second, std::to_string(1.0));
}

TEST_F(NotifierTest, IdleResetsProgressDedupe) {
    publish(notifier, snap(Status::Ready, 1.0));
    publish(notifier, snap(Status::Idle));
    publish(notifier, snap(Status::Ready, 1.0));
    EXPECT_EQ(rec->times.size(), 2u);
}

TEST_F(NotifierTest, NotificationsMapToPlayerEvents) {
    notifier.notify(SeekDone{});
    notifier.notify(ErrorEvent{Error::PipelineDecodeError, true});
    ASSERT_EQ(rec->events.size(), 2u);
    EXPECT_EQ(rec->events[0].first, PlayerEvent::SeekDone);
    EXPECT_EQ(rec->events[1].first, PlayerEvent::OnError);
    EXPECT_EQ(rec->events[1].second,
              std::to_string(static_cast<uint32_t>(Error::PipelineDecodeError)) + "|0|");
}

TEST_F(NotifierTest, ErrorPayloadCarriesTheOriginCodeAndTheHost) {
    notifier.notify(ErrorEvent{Error::HttpError, true, ErrorDetail{404, "media.example.com"}});
    ASSERT_EQ(rec->events.size(), 1u);
    EXPECT_EQ(rec->events[0].second,
              std::to_string(static_cast<uint32_t>(Error::HttpError)) + "|404|media.example.com");
}

TEST_F(NotifierTest, PlayEndAndFirstFrameMapToPlayerEvents) {
    notifier.notify(PlayEnd{});
    notifier.notify(FirstFrameRendered{});
    ASSERT_EQ(rec->events.size(), 2u);
    EXPECT_EQ(rec->events[0].first, PlayerEvent::PlayEnd);
    EXPECT_EQ(rec->events[1].first, PlayerEvent::FirstFrameRendered);
}

TEST_F(NotifierTest, DestroyedObserverIsSkipped) {
    rec.reset();
    publish(notifier, snap(Status::Ready, 1.0));
    notifier.notify(SeekDone{});
}

TEST_F(NotifierTest, RemoveObserverInsideCallbackDoesNotDeadlock) {
    IPlayerObserverPtr weak = rec;
    rec->onState = [&] {
        notifier.removeObserver(weak);
        notifier.addObserver(weak);
    };
    publish(notifier, snap(Status::Ready, 0, true));
    EXPECT_EQ(rec->states.size(), 1u);
}

TEST_F(NotifierTest, RemovedObserverReceivesNothing) {
    notifier.removeObserver(rec);
    publish(notifier, snap(Status::Ready, 1.0));
    EXPECT_TRUE(rec->states.empty());
}

TEST_F(NotifierTest, MultipleObserversAllReceive) {
    auto other = std::make_shared<Recorder>();
    notifier.addObserver(other);
    publish(notifier, snap(Status::Ready, 0, true));
    EXPECT_EQ(rec->states.size(), 1u);
    EXPECT_EQ(other->states.size(), 1u);
}

TEST(NotifierExecutorTest, CallbacksGoThroughTheInjectedExecutorInOrder) {
    auto exec = std::make_shared<QueueExecutor>();
    Notifier notifier("p1", exec);
    auto rec = std::make_shared<Recorder>();
    notifier.addObserver(rec);

    publish(notifier, snap(Status::Ready, 0, true));
    notifier.notify(SeekDone{});
    EXPECT_TRUE(rec->states.empty());
    EXPECT_FALSE(exec->tasks.empty());

    exec->drain();
    EXPECT_EQ(rec->states.size(), 1u);
    EXPECT_EQ(rec->events.size(), 1u);
}

TEST(NotifierExecutorTest, ObserverDroppedBeforeTheExecutorRunsIsSkipped) {
    auto exec = std::make_shared<QueueExecutor>();
    Notifier notifier("p1", exec);
    auto rec = std::make_shared<Recorder>();
    notifier.addObserver(rec);
    publish(notifier, snap(Status::Ready, 0, true));
    rec.reset();
    exec->drain();
}
