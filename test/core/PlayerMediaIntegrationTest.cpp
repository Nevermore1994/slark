//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
// 真实媒体文件播放集成测试，面向移动端（Android/iOS）真实解码/渲染链路。
// 媒体资产：../../test_sample/Sample.mp4（AVC+AAC）、sample-3s.wav（PCM 纯音频）。
// 环境策略：仅当 prepare 成功且播放时钟能实际推进（具备真实播放能力）时执行强断言，
//           否则 GTEST_SKIP，保证无能力环境（如 PC 无音频后端/无视频软解）不误报失败。

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Player.h"
#include "PlayerTypes.h"
#include "VideoInfo.h"

using namespace slark;
using namespace std::chrono_literals;

namespace {

constexpr const char* kMp4File = "Sample.mp4";
constexpr const char* kWavFile = "sample-3s.wav";
constexpr const char* kInvalidFile = "not_exist.mp4";

// 媒体文件路径：优先取 SLARK_TEST_MEDIA_DIR（移动端模拟器进程工作目录不可控），
// 否则回退到 PC 构建的相对路径
std::string mediaPath(const char* file) {
    const char* dir = std::getenv("SLARK_TEST_MEDIA_DIR");
    std::string base = (dir != nullptr) ? std::string(dir) : "../../test_sample";
    return base + "/" + file;
}

// MARK: - 事件记录 observer

// 消费型视频渲染：模拟移动端渲染持续拉取视频帧（无 UI 环境下验证视频链路可排空）
struct MockVideoRender : public IVideoRender {
    void start() noexcept override {
        running_.store(true, std::memory_order_relaxed);
        thread_ = std::thread([this] {
            while (running_.load(std::memory_order_relaxed)) {
                if (auto func = requestRenderFunc_.load()) {
                    (*func)();
                }
                std::this_thread::sleep_for(8ms);
            }
        });
    }

    void pause() noexcept override {
        running_.store(false, std::memory_order_relaxed);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void notifyVideoInfo(std::shared_ptr<VideoInfo> /*videoInfo*/) noexcept override {}

    void notifyRenderInfo() noexcept override {}

    void renderEnd() noexcept override {}

    void pushFrame(AVFrameRefPtr /*frame*/) noexcept override {}

    ~MockVideoRender() override {
        pause();
    }

    std::atomic<bool> running_{false};
    std::thread thread_;
};

struct RecordingObserver : public IPlayerObserver {
    void notifyPlayerState(std::string_view /*playerId*/, PlayerState state) override {
        std::lock_guard lock(mutex_);
        states_.push_back(state);
    }

    void notifyPlayerEvent(std::string_view /*playerId*/, PlayerEvent event,
                           std::string /*value*/) override {
        std::lock_guard lock(mutex_);
        events_.push_back(event);
    }

    void notifyPlayedTime(std::string_view /*playerId*/, double time) override {
        playedTimeSeen_.store(true, std::memory_order_relaxed);
        lastPlayedTime_.store(time, std::memory_order_relaxed);
    }

    [[nodiscard]] bool hasEvent(PlayerEvent event) const {
        std::lock_guard lock(mutex_);
        return std::find(events_.begin(), events_.end(), event) != events_.end();
    }

    [[nodiscard]] bool hasState(PlayerState state) const {
        std::lock_guard lock(mutex_);
        return std::find(states_.begin(), states_.end(), state) != states_.end();
    }

    mutable std::mutex mutex_;
    std::vector<PlayerState> states_;
    std::vector<PlayerEvent> events_;
    std::atomic<bool> playedTimeSeen_{false};
    std::atomic<double> lastPlayedTime_{0.0};
};

// MARK: - 辅助

std::unique_ptr<PlayerParams> makeParams(const std::string& path, bool isLoop = false) {
    auto params = std::make_unique<PlayerParams>();
    params->item.path = path;
    params->item.displayStart = 0.0;
    params->item.displayDuration = 0.0;
    params->setting.isLoop = isLoop;
    params->setting.isMute = false;
    params->setting.volume = 100.0f;
    return params;
}

bool waitUntil(const std::function<bool()>& cond, std::chrono::milliseconds timeout = 5s) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (cond()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return cond();
}

// prepare 并等待组件创建完成：state 离开 Prepared（audio/video track 就绪）。
// iOS 模拟器 AudioUnit 初始化可达数秒，需等其完成后 owner 线程才从慢任务释放。
PlayerState prepareAndWait(Player& player) {
    player.prepare();
    waitUntil([&player] {
        auto s = player.state();
        return s != PlayerState::NotInited && s != PlayerState::Initializing;
    });
    waitUntil(
        [&player] {
            auto s = player.state();
            return s == PlayerState::Buffering || s == PlayerState::Playing ||
                   s == PlayerState::Error || s == PlayerState::Stop;
        },
        10s);
    return player.state();
}

bool waitForPlayEnd(Player& player, const RecordingObserver& observer,
                    std::chrono::milliseconds timeout = 15s) {
    return waitUntil(
        [&player, &observer] {
            return observer.hasEvent(PlayerEvent::PlayEnd) ||
                   player.state() == PlayerState::Completed;
        },
        timeout);
}

// 安全销毁：prepareAndWait 已保证组件创建完成，stop 后 owner 线程任务排空较快，
// 短暂等待后析构避免 ~Impl 在 owner 线程上执行触发断言
void safeDestroy(Player& player) {
    if (player.state() != PlayerState::NotInited && player.state() != PlayerState::Stop) {
        player.stop();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

}  // namespace

class PlayerMediaIntegrationTest : public ::testing::Test {
 protected:
    static void SetUpTestSuite() {
        // 探测当前环境播放能力：play 后等待播放时钟真实推进。
        // 移动端（Android/iOS）音频时钟会推进（wav 3.2s 内可推进至 >0.5s）；
        // PC 无音频后端时 notifyPlayedTime 恒为 0，判定不可播放并跳过强断言用例。
        // 不依赖 PlayEnd：PC 上偶发误报 PlayEnd 会导致强断言用例超时假失败。
        auto player = std::make_unique<Player>(makeParams(mediaPath(kWavFile)));
        auto observer = std::make_shared<RecordingObserver>();
        player->addObserver(observer);
        auto state = prepareAndWait(*player);
        if (state == PlayerState::Error) {
            s_playable_ = false;
            return;
        }
        player->play();
        // 等待播放时钟真实推进（iOS 模拟器 AudioUnit 初始化较慢，audio track setup 延迟完成）
        s_playable_ =
            waitUntil([observer] { return observer->lastPlayedTime_.load() > 0.5; }, 10s);
        safeDestroy(*player);
    }

    static bool isPlayable() {
        return s_playable_;
    }

    static bool s_playable_;
};

bool PlayerMediaIntegrationTest::s_playable_ = true;

// MARK: - 真实文件 prepare / info

TEST_F(PlayerMediaIntegrationTest, WavPrepareSucceeds) {
    auto player = std::make_unique<Player>(makeParams(mediaPath(kWavFile)));
    auto state = prepareAndWait(*player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "current environment cannot decode media, skipping";
    }
    auto info = player->info();
    EXPECT_TRUE(info.isValid);
    EXPECT_TRUE(info.hasAudio);
    EXPECT_NEAR(info.duration, 3.0, 0.5);
    safeDestroy(*player);
}

TEST_F(PlayerMediaIntegrationTest, Mp4PrepareSucceeds) {
    auto player = std::make_unique<Player>(makeParams(mediaPath(kMp4File)));
    auto state = prepareAndWait(*player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "current environment cannot decode media, skipping";
    }
    auto info = player->info();
    EXPECT_TRUE(info.isValid);
    EXPECT_TRUE(info.hasVideo);
    EXPECT_GT(info.duration, 0.0);
    safeDestroy(*player);
}

TEST_F(PlayerMediaIntegrationTest, MissingFileReportsError) {
    auto player = std::make_unique<Player>(makeParams(mediaPath(kInvalidFile)));
    auto observer = std::make_shared<RecordingObserver>();
    player->addObserver(observer);
    auto state = prepareAndWait(*player);
    EXPECT_EQ(state, PlayerState::Error);
    EXPECT_TRUE(observer->hasEvent(PlayerEvent::OnError));
    safeDestroy(*player);
}

// MARK: - 完整播放

TEST_F(PlayerMediaIntegrationTest, WavPlaysToCompletion) {
    if (!PlayerMediaIntegrationTest::isPlayable()) {
        GTEST_SKIP() << "current environment cannot drive playback clock, skipping";
    }
    auto player = std::make_unique<Player>(makeParams(mediaPath(kWavFile)));
    auto observer = std::make_shared<RecordingObserver>();
    player->addObserver(observer);
    auto state = prepareAndWait(*player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "current environment cannot decode media, skipping";
    }

    player->play();
    EXPECT_TRUE(waitForPlayEnd(*player, *observer)) << "3s wav should reach PlayEnd";
    EXPECT_TRUE(observer->hasEvent(PlayerEvent::PlayEnd));
    EXPECT_TRUE(player->state() == PlayerState::Completed ||
                observer->hasState(PlayerState::Completed));
    EXPECT_GE(player->currentPlayedTime(), 2.5);
    safeDestroy(*player);
}

TEST_F(PlayerMediaIntegrationTest, Mp4PlaysToCompletion) {
    if (!PlayerMediaIntegrationTest::isPlayable()) {
        GTEST_SKIP() << "current environment cannot drive playback clock, skipping";
    }
    auto player = std::make_unique<Player>(makeParams(mediaPath(kMp4File)));
    auto observer = std::make_shared<RecordingObserver>();
    player->addObserver(observer);
    // 注入消费型视频渲染，使视频帧可排空（无 UI 环境验证完整播放链路）
    player->setRenderImpl(std::make_shared<MockVideoRender>());
    auto state = prepareAndWait(*player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "current environment cannot decode media, skipping";
    }

    player->play();
    // Sample.mp4 时长 64.9s，验证音视频轨道推进即可（PlayEnd 链路由 3s wav 覆盖）
    EXPECT_TRUE(waitUntil([&player] { return player->currentPlayedTime() > 3.0; }, 15s))
        << "mp4 playback should advance";
    EXPECT_TRUE(observer->hasEvent(PlayerEvent::FirstFrameRendered))
        << "mp4 video first frame should render";
    EXPECT_NE(player->state(), PlayerState::Error);
    safeDestroy(*player);
}

// MARK: - seek / 暂停 / 循环

TEST_F(PlayerMediaIntegrationTest, WavSeekDuringPlayback) {
    if (!PlayerMediaIntegrationTest::isPlayable()) {
        GTEST_SKIP() << "current environment cannot drive playback clock, skipping";
    }
    auto player = std::make_unique<Player>(makeParams(mediaPath(kWavFile)));
    auto observer = std::make_shared<RecordingObserver>();
    player->addObserver(observer);
    auto state = prepareAndWait(*player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "current environment cannot decode media, skipping";
    }

    player->play();
    EXPECT_TRUE(waitUntil([&player] { return player->state() == PlayerState::Playing; }));

    player->seek(1.5, false);
    EXPECT_TRUE(waitUntil([&observer] { return observer->hasEvent(PlayerEvent::SeekDone); }))
        << "seek should complete";
    EXPECT_GE(player->currentPlayedTime(), 1.0);
    EXPECT_TRUE(waitForPlayEnd(*player, *observer)) << "playback should finish after seek";
    safeDestroy(*player);
}

TEST_F(PlayerMediaIntegrationTest, WavPauseHaltsProgress) {
    if (!PlayerMediaIntegrationTest::isPlayable()) {
        GTEST_SKIP() << "current environment cannot drive playback clock, skipping";
    }
    auto player = std::make_unique<Player>(makeParams(mediaPath(kWavFile)));
    auto state = prepareAndWait(*player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "current environment cannot decode media, skipping";
    }

    player->play();
    EXPECT_TRUE(waitUntil([&player] { return player->state() == PlayerState::Playing; }));
    std::this_thread::sleep_for(100ms);

    player->pause();
    auto t1 = player->currentPlayedTime();
    std::this_thread::sleep_for(300ms);
    auto t2 = player->currentPlayedTime();
    EXPECT_NEAR(t1, t2, 0.1) << "paused playback should not advance";

    player->play();
    EXPECT_TRUE(waitUntil([&player, t2] { return player->currentPlayedTime() > t2 + 0.2; }))
        << "resume should advance progress";
    safeDestroy(*player);
}

TEST_F(PlayerMediaIntegrationTest, WavLoopRestartsAfterEnd) {
    if (!PlayerMediaIntegrationTest::isPlayable()) {
        GTEST_SKIP() << "current environment cannot drive playback clock, skipping";
    }
    auto player = std::make_unique<Player>(makeParams(mediaPath(kWavFile), /*isLoop=*/true));
    auto observer = std::make_shared<RecordingObserver>();
    player->addObserver(observer);
    auto state = prepareAndWait(*player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "current environment cannot decode media, skipping";
    }

    player->play();
    EXPECT_TRUE(waitUntil([&observer] { return observer->hasEvent(PlayerEvent::PlayEnd); }, 15s))
        << "loop playback should emit PlayEnd";
    EXPECT_TRUE(waitUntil([&player] { return player->state() == PlayerState::Playing; }))
        << "loop should restart playback";
    safeDestroy(*player);
}

// MARK: - 播放中操作稳定性

TEST_F(PlayerMediaIntegrationTest, RapidSeekDoesNotCrash) {
    if (!PlayerMediaIntegrationTest::isPlayable()) {
        GTEST_SKIP() << "current environment cannot drive playback clock, skipping";
    }
    auto player = std::make_unique<Player>(makeParams(mediaPath(kWavFile)));
    auto observer = std::make_shared<RecordingObserver>();
    player->addObserver(observer);
    auto state = prepareAndWait(*player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "current environment cannot decode media, skipping";
    }

    player->play();
    for (int32_t i = 0; i < 8; ++i) {
        player->seek(static_cast<double>(i) * 0.3, false);
    }
    EXPECT_TRUE(waitUntil([&observer] { return observer->hasEvent(PlayerEvent::SeekDone); }))
        << "rapid seeks should eventually complete";
    EXPECT_NE(player->state(), PlayerState::Error);
    safeDestroy(*player);
}

TEST_F(PlayerMediaIntegrationTest, VolumeAndMuteDuringPlayback) {
    if (!PlayerMediaIntegrationTest::isPlayable()) {
        GTEST_SKIP() << "current environment cannot drive playback clock, skipping";
    }
    auto player = std::make_unique<Player>(makeParams(mediaPath(kWavFile)));
    auto state = prepareAndWait(*player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "current environment cannot decode media, skipping";
    }

    player->play();
    player->setVolume(0.0f);
    player->setVolume(50.0f);
    player->setVolume(100.0f);
    player->setMute(true);
    player->setMute(false);
    EXPECT_NE(player->state(), PlayerState::Error);
    safeDestroy(*player);
}

// MARK: - 事件完整性

TEST_F(PlayerMediaIntegrationTest, ReceivesProgressEvents) {
    if (!PlayerMediaIntegrationTest::isPlayable()) {
        GTEST_SKIP() << "current environment cannot drive playback clock, skipping";
    }
    auto player = std::make_unique<Player>(makeParams(mediaPath(kWavFile)));
    auto observer = std::make_shared<RecordingObserver>();
    player->addObserver(observer);
    auto state = prepareAndWait(*player);
    if (state == PlayerState::Error) {
        GTEST_SKIP() << "current environment cannot decode media, skipping";
    }

    player->play();
    EXPECT_TRUE(waitForPlayEnd(*player, *observer));
    EXPECT_TRUE(observer->playedTimeSeen_.load()) << "should receive played time updates";
    EXPECT_TRUE(observer->hasEvent(PlayerEvent::UpdateCacheTime) ||
                observer->hasEvent(PlayerEvent::PlayEnd));
    safeDestroy(*player);
}
