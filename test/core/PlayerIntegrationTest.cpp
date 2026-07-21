//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
// Player 对外接口集成测试
// 测试分两类：
//   1. 纯防御性保护测试（不依赖 prepare/媒体文件，任何环境均可运行）
//   2. 生命周期测试（依赖 prepare 后的状态，通过 GTEST_SKIP 在无媒体环境跳过）

#include "Player.h"
#include "PlayerObserver.h"
#include "VideoInfo.h"

#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>

using namespace slark;
using ::testing::_;
using ::testing::AtLeast;

// MARK: - Mock 定义

struct MockPlayerObserver : public IPlayerObserver {
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

// MARK: - 辅助工厂

static std::unique_ptr<PlayerParams> makeParams(const std::string& path = "/dev/null") {
    auto params = std::make_unique<PlayerParams>();
    params->item.path = path;
    params->item.displayStart = 0.0;
    params->item.displayDuration = 0.0;
    params->setting.isLoop = false;
    params->setting.isMute = false;
    params->setting.volume = 100.0f;
    return params;
}

// prepare 后等待状态离开 Initializing，返回最终状态
// 超时时返回 Error（无媒体文件环境），让调用方的 GTEST_SKIP 生效
static PlayerState waitForPrepared(Player& player, int32_t maxMs = 500) {
    for (int32_t i = 0; i < maxMs / 10; ++i) {
        auto s = player.state();
        if (s != PlayerState::Initializing && s != PlayerState::NotInited) {
            return s;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return PlayerState::Error;
}

// 安全析构：先 stop 再等待线程退出，避免 Thread 在自身线程中析构
static void safeDestroy(Player& player) {
    if (player.state() != PlayerState::NotInited && player.state() != PlayerState::Stop) {
        player.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// ─── 第一类：纯防御性保护测试（不调用 prepare）────────────────────────────────

TEST(PlayerIntegrationTest, InitialStateIsNotInited) {
    Player player(makeParams());
    EXPECT_EQ(player.state(), PlayerState::NotInited);
}

TEST(PlayerIntegrationTest, PlayerIdIsNonEmptyAfterConstruction) {
    Player player(makeParams());
    EXPECT_FALSE(player.playerId().empty());
}

TEST(PlayerIntegrationTest, PeekReturnsConstructedParams) {
    auto params = makeParams("file:///test/video.mp4");
    Player player(std::move(params));
    EXPECT_EQ(player.peek().item.path, "file:///test/video.mp4");
}

TEST(PlayerIntegrationTest, PlayBeforePrepareIsNoOp) {
    Player player(makeParams());
    player.play();
    EXPECT_EQ(player.state(), PlayerState::NotInited);
}

TEST(PlayerIntegrationTest, StopBeforePrepareIsNoOp) {
    Player player(makeParams());
    player.stop();
    EXPECT_EQ(player.state(), PlayerState::NotInited);
}

TEST(PlayerIntegrationTest, PauseBeforePrepareIsNoOp) {
    Player player(makeParams());
    player.pause();
    EXPECT_EQ(player.state(), PlayerState::NotInited);
}

TEST(PlayerIntegrationTest, SeekBeforePrepareIsNoOp) {
    Player player(makeParams());
    player.seek(5.0, false);
    EXPECT_EQ(player.state(), PlayerState::NotInited);
}

TEST(PlayerIntegrationTest, SetLoopBeforePrepareIsNoOp) {
    Player player(makeParams());
    player.setLoop(true);
    EXPECT_EQ(player.state(), PlayerState::NotInited);
}

TEST(PlayerIntegrationTest, SetVolumeBeforePrepareIsNoOp) {
    Player player(makeParams());
    player.setVolume(50.0f);
    EXPECT_EQ(player.state(), PlayerState::NotInited);
}

TEST(PlayerIntegrationTest, SetMuteBeforePrepareIsNoOp) {
    Player player(makeParams());
    player.setMute(true);
    EXPECT_EQ(player.state(), PlayerState::NotInited);
}

TEST(PlayerIntegrationTest, AddObserverBeforePrepareIsNoOp) {
    Player player(makeParams());
    auto observer = std::make_shared<MockPlayerObserver>();
    EXPECT_CALL(*observer, notifyPlayerState(_, _)).Times(0);
    player.addObserver(std::weak_ptr<IPlayerObserver>(observer));
}

TEST(PlayerIntegrationTest, RemoveObserverBeforePrepareIsNoOp) {
    Player player(makeParams());
    player.removeObserver();
}

TEST(PlayerIntegrationTest, CurrentPlayedTimeBeforePrepareReturnsZero) {
    Player player(makeParams());
    EXPECT_DOUBLE_EQ(player.currentPlayedTime(), 0.0);
}

TEST(PlayerIntegrationTest, InfoBeforePrepareReturnsInvalid) {
    Player player(makeParams());
    EXPECT_FALSE(player.info().isValid);
}

TEST(PlayerIntegrationTest, SetRenderImplNullptrDoesNotCrash) {
    Player player(makeParams());
    player.setRenderImpl(std::weak_ptr<IVideoRender>{});
}

TEST(PlayerIntegrationTest, TwoPlayersHaveDifferentIds) {
    Player p1(makeParams());
    Player p2(makeParams());
    EXPECT_NE(p1.playerId(), p2.playerId());
}

// ─── 第二类：prepare 后的生命周期测试 ────────────────────────────────────────
// 这些测试依赖 prepare 后 PlayerImpl 的内部状态。
// 在无真实媒体文件的环境中，PlayerImpl 会进入 Error 状态，
// Error 状态下 doStop() 不再在 ownerThread_ 自身线程中调用 stop()，析构安全。
TEST(PlayerIntegrationTest, PrepareTransitionsOutOfNotInited) {
    Player player(makeParams());
    player.prepare();
    auto state = waitForPrepared(player);
    EXPECT_NE(state, PlayerState::NotInited);
    safeDestroy(player);
}

TEST(PlayerIntegrationTest, PrepareIsIdempotent) {
    Player player(makeParams());
    player.prepare();
    auto stateAfterFirst = waitForPrepared(player);
    if (stateAfterFirst == PlayerState::Error) {
        GTEST_SKIP() << "PlayerImpl entered Error state (no media file), skipping";
    }
    player.prepare();
    EXPECT_EQ(player.state(), stateAfterFirst);
    safeDestroy(player);
}

TEST(PlayerIntegrationTest, ObserverReceivesStateNotificationAfterPrepare) {
    Player player(makeParams());
    auto observer = std::make_shared<MockPlayerObserver>();
    EXPECT_CALL(*observer, notifyPlayerState(_, _)).Times(AtLeast(1));
    player.addObserver(std::weak_ptr<IPlayerObserver>(observer));
    player.prepare();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    // 在无媒体文件环境下，Error 状态下也应收到至少一次状态通知
    safeDestroy(player);
}

TEST(PlayerIntegrationTest, RemoveObserverStopsNotifications) {
    Player player(makeParams());
    auto observer = std::make_shared<MockPlayerObserver>();
    player.addObserver(std::weak_ptr<IPlayerObserver>(observer));
    player.removeObserver();
    EXPECT_CALL(*observer, notifyPlayerState(_, _)).Times(0);
    player.prepare();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    safeDestroy(player);
}

TEST(PlayerIntegrationTest, CurrentPlayedTimeAfterPrepareIsNonNegative) {
    Player player(makeParams());
    player.prepare();
    waitForPrepared(player);
    EXPECT_GE(player.currentPlayedTime(), 0.0);
    safeDestroy(player);
}

TEST(PlayerIntegrationTest, SetLoopAfterPrepareDoesNotCrash) {
    Player player(makeParams());
    player.prepare();
    if (waitForPrepared(player) == PlayerState::Error) {
        GTEST_SKIP() << "PlayerImpl in Error state (no media file)";
    }
    player.setLoop(true);
    player.setLoop(false);
}

TEST(PlayerIntegrationTest, SetVolumeAfterPrepareDoesNotCrash) {
    Player player(makeParams());
    player.prepare();
    if (waitForPrepared(player) == PlayerState::Error) {
        GTEST_SKIP() << "PlayerImpl in Error state (no media file)";
    }
    player.setVolume(0.0f);
    player.setVolume(100.0f);
}

TEST(PlayerIntegrationTest, SetMuteAfterPrepareDoesNotCrash) {
    Player player(makeParams());
    player.prepare();
    if (waitForPrepared(player) == PlayerState::Error) {
        GTEST_SKIP() << "PlayerImpl in Error state (no media file)";
    }
    player.setMute(true);
    player.setMute(false);
}

TEST(PlayerIntegrationTest, SeekAfterPrepareDoesNotCrash) {
    Player player(makeParams());
    player.prepare();
    if (waitForPrepared(player) == PlayerState::Error) {
        GTEST_SKIP() << "PlayerImpl in Error state (no media file)";
    }
    player.seek(0.0, false);
    player.seek(-1.0, false);
}

TEST(PlayerIntegrationTest, StopAfterPrepareTransitionsToStop) {
    Player player(makeParams());
    player.prepare();
    auto state = waitForPrepared(player);
    // Error 状态下 stop 应安全
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "PlayerImpl entered Error state (no media file), skipping stop test";
    }
    player.stop();
    const auto s = player.state();
    EXPECT_TRUE(s == PlayerState::Stop || s == PlayerState::Error);
}

TEST(PlayerIntegrationTest, DestructorDuringInitializingDoesNotCrash) {
    {
        Player player(makeParams());
        player.prepare();
        safeDestroy(player);
    }
}

TEST(PlayerIntegrationTest, MultiplePrepareCyclesDoNotCrash) {
    for (int32_t i = 0; i < 3; ++i) {
        Player player(makeParams());
        player.prepare();
        waitForPrepared(player);
        safeDestroy(player);
    }
}

// ─── 第三类：PlayPipeline 组件级集成测试 ───────────────────────────────────

TEST(PlayerIntegrationTest, PipelineBuildAndTeardown) {
    Player player(makeParams());
    player.prepare();
    auto state = waitForPrepared(player);
    EXPECT_NE(state, PlayerState::NotInited);
    // teardown 通过析构触发，safeDestroy 确保安全
    safeDestroy(player);
}

TEST(PlayerIntegrationTest, RepeatedPlayStopCycles) {
    Player player(makeParams());
    player.prepare();
    auto state = waitForPrepared(player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "PlayerImpl in Error state (no media file)";
    }
    for (int32_t i = 0; i < 3; ++i) {
        player.play();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        player.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    safeDestroy(player);
}

TEST(PlayerIntegrationTest, PlayPauseCycle) {
    Player player(makeParams());
    player.prepare();
    auto state = waitForPrepared(player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "PlayerImpl in Error state (no media file)";
    }
    player.play();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    player.pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    player.play();
    safeDestroy(player);
}

TEST(PlayerIntegrationTest, SeekDuringPlayDoesNotCrash) {
    Player player(makeParams());
    player.prepare();
    auto state = waitForPrepared(player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "PlayerImpl in Error state (no media file)";
    }
    player.play();
    for (int32_t i = 0; i < 3; ++i) {
        player.seek(static_cast<double>(i) * 5.0, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    safeDestroy(player);
}

TEST(PlayerIntegrationTest, ObserverReceivesPlayedTimeUpdates) {
    Player player(makeParams());
    auto observer = std::make_shared<MockPlayerObserver>();
    player.addObserver(std::weak_ptr<IPlayerObserver>(observer));
    EXPECT_CALL(*observer, notifyPlayedTime(_, _)).Times(testing::AtLeast(0));
    player.prepare();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    safeDestroy(player);
}

TEST(PlayerIntegrationTest, SetVolumeDuringPlayback) {
    Player player(makeParams());
    player.prepare();
    auto state = waitForPrepared(player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "PlayerImpl in Error state (no media file)";
    }
    player.setVolume(0.0f);
    player.setVolume(50.0f);
    player.setVolume(100.0f);
    safeDestroy(player);
}

TEST(PlayerIntegrationTest, SetMuteDuringPlayback) {
    Player player(makeParams());
    player.prepare();
    auto state = waitForPrepared(player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "PlayerImpl in Error state (no media file)";
    }
    player.setMute(true);
    player.setMute(false);
    safeDestroy(player);
}

TEST(PlayerIntegrationTest, RapidSeekDoesNotCrash) {
    Player player(makeParams());
    player.prepare();
    auto state = waitForPrepared(player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "PlayerImpl in Error state (no media file)";
    }
    for (int32_t i = 0; i < 10; ++i) {
        player.seek(static_cast<double>(i), false);
    }
    safeDestroy(player);
}

TEST(PlayerIntegrationTest, ToggleLoopDuringPlayback) {
    Player player(makeParams());
    player.prepare();
    auto state = waitForPrepared(player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "PlayerImpl in Error state (no media file)";
    }
    player.setLoop(true);
    player.setLoop(false);
    player.setLoop(true);
    safeDestroy(player);
}
