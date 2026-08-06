//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "NotifyCenter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "Player.h"

using namespace slark;
using ::testing::_;
using ::testing::Eq;
using ::testing::InSequence;


class MockObserver : public IPlayerObserver {
 public:
    MOCK_METHOD(void,
                notifyPlayerState,
                (std::string_view playerId, PlayerState state),
                (override));
    MOCK_METHOD(void,
                notifyPlayerEvent,
                (std::string_view playerId, PlayerEvent event, std::string value),
                (override));
    MOCK_METHOD(void, notifyPlayedTime, (std::string_view playerId, double time), (override));
};


TEST(NotifyCenterTest, NotifiesStateAfterAddObserver) {
    NotifyCenter center("player-1");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayerState("player-1", PlayerState::Playing)).Times(1);

    center.addObserver(observer);
    center.notifyPlayerState(PlayerState::Playing);
}

TEST(NotifyCenterTest, NoNotifyAfterRemoveObserver) {
    NotifyCenter center("player-1");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayerState(_, _)).Times(0);

    center.addObserver(observer);
    center.removeObserver();
    center.notifyPlayerState(PlayerState::Playing);
}

TEST(NotifyCenterTest, NoNotifyWhenObserverExpired) {
    NotifyCenter center("player-1");
    {
        auto observer = std::make_shared<MockObserver>();
        center.addObserver(observer);
    }
    center.notifyPlayerState(PlayerState::Playing);
}


TEST(NotifyCenterTest, NotifiesPlayerEvent) {
    NotifyCenter center("player-2");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayerEvent("player-2", PlayerEvent::SeekDone, _)).Times(1);

    center.addObserver(observer);
    center.notifyPlayerEvent(PlayerEvent::SeekDone);
}

TEST(NotifyCenterTest, NotifiesPlayerEventWithValue) {
    NotifyCenter center("player-2");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayerEvent("player-2", PlayerEvent::UpdateCacheTime, "5.5"))
        .Times(1);

    center.addObserver(observer);
    center.notifyPlayerEvent(PlayerEvent::UpdateCacheTime, "5.5");
}


TEST(NotifyCenterTest, PlayedTimeNotifiesOnFirstCall) {
    NotifyCenter center("player-3");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayedTime("player-3", 3.0)).Times(1);

    center.addObserver(observer);
    center.notifyPlayedTime(10.0, 3.0, 3.0, true, true);
}

TEST(NotifyCenterTest, PlayedTimeThrottlesWithin100ms) {
    NotifyCenter center("player-3");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayedTime("player-3", 3.0)).Times(1);
    EXPECT_CALL(*observer, notifyPlayedTime("player-3", 3.05)).Times(0);

    center.addObserver(observer);
    center.notifyPlayedTime(10.0, 3.0, 3.0, true, true);
    center.notifyPlayedTime(10.0, 3.05, 3.05, true, true);
}

TEST(NotifyCenterTest, PlayedTimeNotifiesWhenDiffExceeds100ms) {
    NotifyCenter center("player-3");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayedTime("player-3", 3.0)).Times(1);
    EXPECT_CALL(*observer, notifyPlayedTime("player-3", 3.2)).Times(1);

    center.addObserver(observer);
    center.notifyPlayedTime(10.0, 3.0, 3.0, true, true);
    center.notifyPlayedTime(10.0, 3.2, 3.2, true, true);
}

TEST(NotifyCenterTest, PlayedTimeIsEndTimeAlwaysNotifiesDuration) {
    NotifyCenter center("player-3");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayedTime("player-3", 10.0)).Times(1);

    center.addObserver(observer);
    center.notifyPlayedTime(10.0, 3.0, 3.0, true, true, true);
}


TEST(NotifyCenterTest, ResolveCurrentTimeAudioAndVideo) {
    NotifyCenter center("player-4");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayedTime("player-4", 2.0)).Times(1);

    center.addObserver(observer);
    center.notifyPlayedTime(10.0, 3.0, 2.0, true, true);
}

TEST(NotifyCenterTest, ResolveCurrentTimeAudioOnly) {
    NotifyCenter center("player-4");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayedTime("player-4", 3.0)).Times(1);

    center.addObserver(observer);
    center.notifyPlayedTime(10.0, 3.0, 0.0, true, false);
}

TEST(NotifyCenterTest, ResolveCurrentTimeVideoOnly) {
    NotifyCenter center("player-4");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayedTime("player-4", 2.5)).Times(1);

    center.addObserver(observer);
    center.notifyPlayedTime(10.0, 0.0, 2.5, false, true);
}

TEST(NotifyCenterTest, ResolveCurrentTimeNoTrack) {
    NotifyCenter center("player-4");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayedTime("player-4", 0.0)).Times(1);

    center.addObserver(observer);
    center.notifyPlayedTime(10.0, 0.0, 0.0, false, false);
}

TEST(NotifyCenterTest, ResolveCurrentTimeClampsToDuration) {
    NotifyCenter center("player-4");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayedTime("player-4", 10.0)).Times(1);

    center.addObserver(observer);
    center.notifyPlayedTime(10.0, 12.0, 12.0, true, false);
}


TEST(NotifyCenterTest, ResetAllowsRenotifyThrottledTime) {
    NotifyCenter center("player-5");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayedTime("player-5", 3.0)).Times(2);

    center.addObserver(observer);
    center.notifyPlayedTime(10.0, 3.0, 3.0, true, true);
    center.resetNotifyState();
    center.notifyPlayedTime(10.0, 3.0, 3.0, true, true);
}


TEST(NotifyCenterTest, CacheTimeNotifiesOnFirstCall) {
    NotifyCenter center("player-6");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayerEvent("player-6", PlayerEvent::UpdateCacheTime, "5.000000"))
        .Times(1);

    center.addObserver(observer);
    center.notifyCacheTime(5.0);
}

TEST(NotifyCenterTest, CacheTimeThrottlesWithin100ms) {
    NotifyCenter center("player-6");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayerEvent("player-6", PlayerEvent::UpdateCacheTime, _)).Times(1);

    center.addObserver(observer);
    center.notifyCacheTime(5.0);
    center.notifyCacheTime(5.05);
}

TEST(NotifyCenterTest, CacheTimeNotifiesWhenDiffExceeds100ms) {
    NotifyCenter center("player-6");
    auto observer = std::make_shared<MockObserver>();

    EXPECT_CALL(*observer, notifyPlayerEvent("player-6", PlayerEvent::UpdateCacheTime, _)).Times(2);

    center.addObserver(observer);
    center.notifyCacheTime(5.0);
    center.notifyCacheTime(5.2);
}
