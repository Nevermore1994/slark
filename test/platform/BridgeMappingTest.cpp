//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
// Pure C++ verification of the platform bridge contract: the Snapshot-to-PlayerState
// mapping the JNI/ObjC layers rely on, and the Notifier's event de-duplication. No device
// or platform headers involved.
//
#include "Notifier.h"

#include <gtest/gtest.h>

#include "PlayerTypes.h"

using namespace slark;
using namespace slark::engine;

namespace {

Snapshot snapshotWith(Status status, bool wantPlay = false) {
    Snapshot s;
    s.status = status;
    s.wantPlay = wantPlay;
    return s;
}

class RecordingObserver : public IPlayerObserver {
 public:
    void notifyPlayedTime(std::string_view, double time) override {
        playedTimes.push_back(time);
    }

    void notifyPlayerState(std::string_view, PlayerState state) override {
        states.push_back(state);
    }

    void notifyPlayerEvent(std::string_view, PlayerEvent event, std::string value) override {
        events.emplace_back(event, value);
    }

    std::vector<PlayerState> states;
    std::vector<double> playedTimes;
    std::vector<std::pair<PlayerEvent, std::string>> events;
};

}  // namespace

TEST(BridgeMappingTest, SnapshotToPlayerStateCoversEveryStatus) {
    EXPECT_EQ(toPlayerState(snapshotWith(Status::Idle)), PlayerState::NotInited);
    EXPECT_EQ(toPlayerState(snapshotWith(Status::Opening)), PlayerState::Buffering);
    EXPECT_EQ(toPlayerState(snapshotWith(Status::Buffering)), PlayerState::Buffering);
    EXPECT_EQ(toPlayerState(snapshotWith(Status::Seeking)), PlayerState::Buffering);
    EXPECT_EQ(toPlayerState(snapshotWith(Status::Ready, /*wantPlay=*/true)), PlayerState::Playing);
    EXPECT_EQ(toPlayerState(snapshotWith(Status::Ready, /*wantPlay=*/false)), PlayerState::Pause);
    EXPECT_EQ(toPlayerState(snapshotWith(Status::Ended)), PlayerState::Completed);
    EXPECT_EQ(toPlayerState(snapshotWith(Status::Failed)), PlayerState::Error);
}

TEST(BridgeMappingTest, StateNotificationsAreDeduplicated) {
    Notifier notifier{"bridge"};
    auto observer = std::make_shared<RecordingObserver>();
    notifier.addObserver(observer);

    notifier.publish(snapshotWith(Status::Opening), Time::TimePoint{});
    notifier.publish(snapshotWith(Status::Buffering), Time::TimePoint{});
    notifier.publish(snapshotWith(Status::Buffering), Time::TimePoint{});
    notifier.publish(snapshotWith(Status::Ready, true), Time::TimePoint{});

    // Opening and Buffering both map to PlayerState::Buffering; the repeat is deduplicated.
    EXPECT_EQ(observer->states.size(), 2u);
    EXPECT_EQ(observer->states[0], PlayerState::Buffering);
    EXPECT_EQ(observer->states[1], PlayerState::Playing);
}

TEST(BridgeMappingTest, CacheTimeUsesSnapshotBufferedWithStringValue) {
    Notifier notifier{"bridge"};
    auto observer = std::make_shared<RecordingObserver>();
    notifier.addObserver(observer);

    auto s = snapshotWith(Status::Ready, true);
    s.buffered = Time::TimePoint::fromSeconds(4.2);
    notifier.publish(s, Time::TimePoint{});
    notifier.publish(s, Time::TimePoint{});  // unchanged buffered: no repeat

    ASSERT_EQ(observer->events.size(), 1u);
    EXPECT_EQ(observer->events[0].first, PlayerEvent::UpdateCacheTime);
    EXPECT_DOUBLE_EQ(std::stod(observer->events[0].second), 4.2);
}

TEST(BridgeMappingTest, SeekDoneAndErrorMapToPlayerEvents) {
    Notifier notifier{"bridge"};
    auto observer = std::make_shared<RecordingObserver>();
    notifier.addObserver(observer);

    notifier.notify(SeekDone{});
    notifier.notify(ErrorEvent{Error::HttpError, false, ErrorDetail{404, "media.example.com"}});

    ASSERT_EQ(observer->events.size(), 2u);
    EXPECT_EQ(observer->events[0].first, PlayerEvent::SeekDone);
    EXPECT_EQ(observer->events[1].first, PlayerEvent::OnError);
    EXPECT_EQ(observer->events[1].second,
              std::to_string(static_cast<uint32_t>(Error::HttpError)) + "|404|media.example.com");
}
