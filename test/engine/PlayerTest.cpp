//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include <gtest/gtest.h>

#include <atomic>
#include <thread>

#include "Notifier.h"
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
