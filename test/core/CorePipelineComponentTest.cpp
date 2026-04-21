//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AVFrame.hpp"
#include "PipelineConfig.h"
#include "PlayerStateMachine.h"
#include "Time.hpp"
#include "pipeline/ClockSync.h"
#include "pipeline/EpochFilter.h"
#include "pipeline/SeekCoordinator.h"
#include "pipeline/VideoFrameContainer.h"

#include <chrono>
#include <gtest/gtest.h>
#include <memory>

using namespace slark;

MARK : -BackpressureConfig 测试

       TEST(BackpressureConfigTest, ForLocalDefaultsAreValid) {
    auto cfg = BackpressureConfig::forLocal();
    EXPECT_TRUE(cfg.isValid());
    EXPECT_FALSE(cfg.isLiveSource);
    EXPECT_EQ(cfg.highWatermarkDuration, 30.0);
    EXPECT_EQ(cfg.lowWatermarkDuration, 5.0);
    EXPECT_EQ(cfg.hardCapDuration, 60.0);
    EXPECT_EQ(cfg.startBufferDuration, 0.0);
    EXPECT_EQ(cfg.rebufferDuration, 1.0);
}

TEST(BackpressureConfigTest, ForNetworkDefaultsAreValid) {
    auto cfg = BackpressureConfig::forNetwork();
    EXPECT_TRUE(cfg.isValid());
    EXPECT_FALSE(cfg.isLiveSource);
    EXPECT_EQ(cfg.highWatermarkDuration, 10.0);
    EXPECT_EQ(cfg.lowWatermarkDuration, 3.0);
    EXPECT_EQ(cfg.hardCapDuration, 20.0);
    EXPECT_EQ(cfg.startBufferDuration, 2.0);
    EXPECT_EQ(cfg.rebufferDuration, 2.0);
}

TEST(BackpressureConfigTest, ForLiveDefaultsAreValid) {
    auto cfg = BackpressureConfig::forLive();
    EXPECT_TRUE(cfg.isValid());
    EXPECT_TRUE(cfg.isLiveSource);
    EXPECT_EQ(cfg.highWatermarkDuration, 5.0);
    EXPECT_EQ(cfg.lowWatermarkDuration, 1.0);
    EXPECT_EQ(cfg.hardCapDuration, 10.0);
    EXPECT_EQ(cfg.startBufferDuration, 1.0);
    EXPECT_EQ(cfg.rebufferDuration, 1.0);
    EXPECT_EQ(cfg.maxDropPerVsync, 4);
}

TEST(BackpressureConfigTest, InvalidWhenLowWatermarkExceedsHigh) {
    auto cfg = BackpressureConfig::forLocal();
    cfg.lowWatermarkDuration = cfg.highWatermarkDuration + 1.0;
    EXPECT_FALSE(cfg.isValid());
}

TEST(BackpressureConfigTest, InvalidWhenHighWatermarkExceedsHardCap) {
    auto cfg = BackpressureConfig::forLocal();
    cfg.highWatermarkDuration = cfg.hardCapDuration + 1.0;
    EXPECT_FALSE(cfg.isValid());
}

TEST(BackpressureConfigTest, InvalidWhenStartBufferExceedsHighWatermark) {
    auto cfg = BackpressureConfig::forLocal();
    cfg.startBufferDuration = cfg.highWatermarkDuration + 1.0;
    EXPECT_FALSE(cfg.isValid());
}

TEST(BackpressureConfigTest, InvalidWhenStartBufferEqualsHighWatermark) {
    auto cfg = BackpressureConfig::forLocal();
    cfg.startBufferDuration = cfg.highWatermarkDuration;
    EXPECT_FALSE(cfg.isValid());
}

TEST(BackpressureConfigTest, ValidBoundaryLowEqualsZero) {
    auto cfg = BackpressureConfig::forLocal();
    cfg.lowWatermarkDuration = 0.0;
    cfg.highWatermarkDuration = 1.0;
    cfg.hardCapDuration = 2.0;
    cfg.startBufferDuration = 0.5;
    EXPECT_TRUE(cfg.isValid());
}

TEST(BackpressureConfigTest, ValidBoundaryLowJustBelowHigh) {
    auto cfg = BackpressureConfig{};
    cfg.lowWatermarkDuration = 0.99;
    cfg.highWatermarkDuration = 1.0;
    cfg.hardCapDuration = 2.0;
    cfg.startBufferDuration = 0.5;
    EXPECT_TRUE(cfg.isValid());
}

TEST(BackpressureConfigTest, ValidBoundaryHighJustBelowHardCap) {
    auto cfg = BackpressureConfig{};
    cfg.lowWatermarkDuration = 0.5;
    cfg.highWatermarkDuration = 1.99;
    cfg.hardCapDuration = 2.0;
    cfg.startBufferDuration = 1.0;
    EXPECT_TRUE(cfg.isValid());
}

TEST(BackpressureConfigTest, ValidBoundaryStartBufferJustBelowHigh) {
    auto cfg = BackpressureConfig{};
    cfg.lowWatermarkDuration = 0.5;
    cfg.highWatermarkDuration = 2.0;
    cfg.hardCapDuration = 3.0;
    cfg.startBufferDuration = 1.99;
    EXPECT_TRUE(cfg.isValid());
}

MARK : -ClockSync 测试

       TEST(ClockSyncTest, EvaluateAVSyncRenderWhenDiffWithinThreshold) {
    diff 在 ±0.1s 内 → Render EXPECT_EQ(ClockSync::evaluateAVSync(0.0), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(0.05), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.05), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(0.099), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(-0.099), AVSyncAction::Render);
}

TEST(ClockSyncTest, EvaluateAVSyncRenderWhenDiffExceedsMaxThreshold) {
    差值过大（≥10s），视为时钟异常 → Render EXPECT_EQ(ClockSync::evaluateAVSync(10.0),
                                                      AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(-10.0), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(100.0), AVSyncAction::Render);
}

TEST(ClockSyncTest, EvaluateAVSyncDropWhenVideoLagsAudio) {
    正 diff（视频落后音频）超过阈值但未异常 → Drop（追帧） EXPECT_EQ(ClockSync::evaluateAVSync(0.2),
                                                                     AVSyncAction::Drop);
    EXPECT_EQ(ClockSync::evaluateAVSync(1.0), AVSyncAction::Drop);
    EXPECT_EQ(ClockSync::evaluateAVSync(5.0), AVSyncAction::Drop);
    EXPECT_EQ(ClockSync::evaluateAVSync(9.99), AVSyncAction::Drop);
}

TEST(ClockSyncTest, EvaluateAVSyncHoldWhenVideoLeadsAudio) {
    负 diff（视频超前音频）超过阈值但未异常 → Hold EXPECT_EQ(ClockSync::evaluateAVSync(-0.2),
                                                             AVSyncAction::Hold);
    EXPECT_EQ(ClockSync::evaluateAVSync(-1.0), AVSyncAction::Hold);
    EXPECT_EQ(ClockSync::evaluateAVSync(-9.99), AVSyncAction::Hold);
}

TEST(ClockSyncTest, EvaluateAVSyncThresholdBoundary) {
    精确边界测试
    EXPECT_EQ(ClockSync::evaluateAVSync(0.1), AVSyncAction::Render);
    刚好等于 kMinThreshold EXPECT_EQ(ClockSync::evaluateAVSync(-0.1), AVSyncAction::Render);
    EXPECT_EQ(ClockSync::evaluateAVSync(0.10001), AVSyncAction::Drop);
    刚好超过 kMinThreshold EXPECT_EQ(ClockSync::evaluateAVSync(-0.10001), AVSyncAction::Hold);
}

TEST(ClockSyncTest, EvaluateAVSyncWithPlaybackRate) {
    变速播放时阈值随速率缩放
    2x 速率下：kMinThreshold* 2.0 = 0.2s, kMaxThreshold* 2.0 = 20.0s

                                          diff =
                                              0.15，在 1x 下应 Drop，在 2x 下应 Render（0.15 <
                                              0.2） EXPECT_EQ(ClockSync::evaluateAVSync(0.15, 2.0),
                                                              AVSyncAction::Render);

    diff = 0.25，在 1x 和 2x 下都应 Drop EXPECT_EQ(ClockSync::evaluateAVSync(0.25, 2.0),
                                                   AVSyncAction::Drop);

    0.5x 速率下：kMinThreshold* 0.5 = 0.05s diff =
        0.08，在 1x 下应 Render，在 0.5x 下应 Drop（0.08 >
        0.05） EXPECT_EQ(ClockSync::evaluateAVSync(0.08, 0.5), AVSyncAction::Drop);

    diff = 0.03，在 0.5x 下应 Render（0.03 <
           0.05） EXPECT_EQ(ClockSync::evaluateAVSync(0.03, 0.5), AVSyncAction::Render);
}

TEST(ClockSyncTest, SetAndGetPlaybackRate) {
    ClockSync clock;
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 1.0);

    clock.setPlaybackRate(2.0);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 2.0);

    clock.setPlaybackRate(0.5);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 0.5);
}

TEST(ClockSyncTest, PlaybackRateClampedToRange) {
    ClockSync clock;

    低于最小值应被忽略
    clock.setPlaybackRate(0.1);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 1.0);
    仍为默认值

    高于最大值应被忽略
    clock.setPlaybackRate(5.0);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 1.0);
}

TEST(ClockSyncTest, PlaybackRateBoundaryValues) {
    ClockSync clock;

    clock.setPlaybackRate(0.5);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 0.5);

    clock.setPlaybackRate(2.0);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 2.0);

    刚好等于边界值应被接受
    clock.setPlaybackRate(0.5);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 0.5);

    clock.setPlaybackRate(2.0);
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 2.0);
}

TEST(ClockSyncTest, ResetClearsPlaybackRate) {
    ClockSync clock;
    clock.setPlaybackRate(1.5);
    clock.reset();
    EXPECT_DOUBLE_EQ(clock.playbackRate(), 1.0);
}

TEST(ClockSyncTest, InitialState) {
    ClockSync clock;
    EXPECT_EQ(clock.audioTime(), Time::TimePoint(0));
    EXPECT_EQ(clock.videoTime(), Time::TimePoint(0));
    EXPECT_FALSE(clock.isTearingDown());
}

TEST(ClockSyncTest, SetAndGetAudioVideoTime) {
    ClockSync clock;
    auto audioTp = Time::TimePoint::fromSeconds(1.5);
    auto videoTp = Time::TimePoint::fromSeconds(1.2);

    clock.setAudioTime(audioTp);
    clock.setVideoTime(videoTp);

    EXPECT_EQ(clock.audioTime(), audioTp);
    EXPECT_EQ(clock.videoTime(), videoTp);
}

TEST(ClockSyncTest, MasterTimePrefersAudioWhenHasAudio) {
    ClockSync clock;
    clock.setAudioTime(Time::TimePoint::fromSeconds(3.0));
    clock.setVideoTime(Time::TimePoint::fromSeconds(2.0));

    EXPECT_EQ(clock.masterTime(true), Time::TimePoint::fromSeconds(3.0));
    EXPECT_EQ(clock.masterTime(false), Time::TimePoint::fromSeconds(2.0));
}

TEST(ClockSyncTest, TeardownFlag) {
    ClockSync clock;
    EXPECT_FALSE(clock.isTearingDown());
    clock.notifyTeardown();
    EXPECT_TRUE(clock.isTearingDown());
}

TEST(ClockSyncTest, ResetClearsTimes) {
    ClockSync clock;
    clock.setAudioTime(Time::TimePoint::fromSeconds(5.0));
    clock.setVideoTime(Time::TimePoint::fromSeconds(4.0));
    clock.reset();
    EXPECT_EQ(clock.audioTime(), Time::TimePoint(0));
    EXPECT_EQ(clock.videoTime(), Time::TimePoint(0));
}

MARK : -SeekCoordinator 测试

       TEST(SeekCoordinatorTest, InitialState) {
    SeekCoordinator sc;
    EXPECT_FALSE(sc.isSeekPending());
    EXPECT_EQ(sc.seekEpoch(), 0u);
    EXPECT_EQ(sc.activeSeekTime(), std::nullopt);
    EXPECT_TRUE(sc.isSeekCompleted(true, true));
    EXPECT_TRUE(sc.isSeekCompleted(false, false));
}

TEST(SeekCoordinatorTest, BeginSeekAdvancesEpoch) {
    SeekCoordinator sc;
    EXPECT_EQ(sc.seekEpoch(), 0u);

    bool superseded = sc.beginSeek(CTime(10.0));
    EXPECT_FALSE(superseded);
    首次 seek，没有覆盖旧 seek EXPECT_EQ(sc.seekEpoch(), 1u);
    EXPECT_TRUE(sc.isSeekPending());
    EXPECT_EQ(sc.activeSeekTime(), 10.0);
}

TEST(SeekCoordinatorTest, BeginSeekSupersedesPendingSeek) {
    SeekCoordinator sc;
    (void)sc.beginSeek(CTime(5.0));
    EXPECT_EQ(sc.seekEpoch(), 1u);

    bool superseded = sc.beginSeek(CTime(10.0));
    EXPECT_TRUE(superseded);
    覆盖了未完成的旧 seek EXPECT_EQ(sc.seekEpoch(), 2u);
    EXPECT_TRUE(sc.isSeekPending());
    EXPECT_EQ(sc.activeSeekTime(), 10.0);
}

TEST(SeekCoordinatorTest, CompleteSeekClearsPendingState) {
    SeekCoordinator sc;
    (void)sc.beginSeek(CTime(5.0));
    EXPECT_TRUE(sc.isSeekPending());

    sc.completeSeek();
    EXPECT_FALSE(sc.isSeekPending());
    EXPECT_EQ(sc.activeSeekTime(), std::nullopt);
}

TEST(SeekCoordinatorTest, SeekNotCompletedUntilBothTracksSeeked) {
    SeekCoordinator sc;
    (void)sc.beginSeek(CTime(5.0));

    只有视频
    EXPECT_FALSE(sc.isSeekCompleted(true, false));
    sc.onVideoSeeked();
    EXPECT_TRUE(sc.isSeekCompleted(true, false));

    重置进行音视频同时 seek 的测试 sc.completeSeek();
    (void)sc.beginSeek(CTime(10.0));

    音视频都有
    EXPECT_FALSE(sc.isSeekCompleted(true, true));
    sc.onVideoSeeked();
    EXPECT_FALSE(sc.isSeekCompleted(true, true));
    sc.onAudioSeeked();
    EXPECT_TRUE(sc.isSeekCompleted(true, true));
}

TEST(SeekCoordinatorTest, IsSeekCompletedWhenNoSeekPending) {
    SeekCoordinator sc;
    没有 pending seek 时，始终返回 true EXPECT_TRUE(sc.isSeekCompleted(true, true));
    EXPECT_TRUE(sc.isSeekCompleted(true, false));
    EXPECT_TRUE(sc.isSeekCompleted(false, true));
    EXPECT_TRUE(sc.isSeekCompleted(false, false));
}

TEST(SeekCoordinatorTest, IsSeekTimedOutWhenNoSeekPending) {
    SeekCoordinator sc;
    EXPECT_FALSE(sc.isSeekTimedOut(std::chrono::milliseconds(5000)));
}

TEST(SeekCoordinatorTest, IsSeekTimedOutWithinTimeout) {
    SeekCoordinator sc;
    (void)sc.beginSeek(CTime(5.0));
    刚开始 seek，不应该超时 EXPECT_FALSE(sc.isSeekTimedOut(std::chrono::milliseconds(5000)));
}

TEST(SeekCoordinatorTest, SeekEpochMonotonicallyIncreasing) {
    SeekCoordinator sc;
    EXPECT_EQ(sc.seekEpoch(), 0u);
    (void)sc.beginSeek(CTime(1.0));
    EXPECT_EQ(sc.seekEpoch(), 1u);
    sc.completeSeek();
    (void)sc.beginSeek(CTime(2.0));
    EXPECT_EQ(sc.seekEpoch(), 2u);
    sc.completeSeek();
    (void)sc.beginSeek(CTime(3.0));
    EXPECT_EQ(sc.seekEpoch(), 3u);
}

MARK : -VideoFrameContainer 测试

       namespace {
    辅助函数：创建带指定 pts 和 epoch 的 TaggedFrame TaggedFrame makeVideoFrame(double pts,
                                                                                uint64_t epoch) {
        auto frame = std::make_shared<AVFrame>(AVFrameType::Video);
        frame->pts = static_cast<int64_t>(pts * 1000);
        frame->timeScale = 1000;
        return {frame, epoch};
    }
}

namespace

    TEST(VideoFrameContainerTest, InitiallyEmpty) {
VideoFrameContainer container;
EXPECT_TRUE(container.empty());
EXPECT_EQ(container.size(), 0u);
EXPECT_EQ(container.backPts(), std::nullopt);
} // namespace TEST(VideoFrameContainerTest,InitiallyEmpty)

TEST(VideoFrameContainerTest, PushAndPopWithDeque) {
    VideoFrameContainer container;
    auto tf1 = makeVideoFrame(1.0, 0);
    auto tf2 = makeVideoFrame(2.0, 0);
    auto tf3 = makeVideoFrame(3.0, 0);

    container.push(tf1);
    container.push(tf2);
    container.push(tf3);
    EXPECT_EQ(container.size(), 3u);
    EXPECT_FALSE(container.empty());

    pop 当前 epoch 的帧 auto frame = container.pop(0);
    ASSERT_TRUE(frame != nullptr);
    EXPECT_DOUBLE_EQ(frame->ptsTime(), 1.0);

    frame = container.pop(0);
    ASSERT_TRUE(frame != nullptr);
    EXPECT_DOUBLE_EQ(frame->ptsTime(), 2.0);

    frame = container.pop(0);
    ASSERT_TRUE(frame != nullptr);
    EXPECT_DOUBLE_EQ(frame->ptsTime(), 3.0);

    无更多帧
    frame = container.pop(0);
    EXPECT_EQ(frame, nullptr);
}

TEST(VideoFrameContainerTest, PeekWithCurrentEpoch) {
    VideoFrameContainer container;
    auto tf1 = makeVideoFrame(1.0, 1);
    auto tf2 = makeVideoFrame(2.0, 1);
    auto tf3 = makeVideoFrame(3.0, 0);
    旧 epoch

        container.push(tf1);
    container.push(tf2);
    container.push(tf3);

    peek 应该跳过旧 epoch 帧，返回第一个当前 epoch 的帧 auto frame = container.peek(1);
    ASSERT_TRUE(frame != nullptr);
    EXPECT_DOUBLE_EQ(frame->ptsTime(), 1.0);
}

TEST(VideoFrameContainerTest, PopSkipsOldEpochFrames) {
    VideoFrameContainer container;
    auto tfOld = makeVideoFrame(1.0, 0);
    auto tfCurrent = makeVideoFrame(2.0, 1);

    container.push(tfOld);
    container.push(tfCurrent);

    pop(1) 应该跳过 epoch = 0 的帧，返回 epoch = 1 的帧 auto frame = container.pop(1);
    ASSERT_TRUE(frame != nullptr);
    EXPECT_DOUBLE_EQ(frame->ptsTime(), 2.0);

    容器中旧 epoch 帧被清理 EXPECT_TRUE(container.empty());
}

TEST(VideoFrameContainerTest, PopReturnsNullptrWhenEmpty) {
    VideoFrameContainer container;
    auto frame = container.pop(0);
    EXPECT_EQ(frame, nullptr);
}

TEST(VideoFrameContainerTest, PopReturnsNullptrWhenOnlyOldEpochFrames) {
    VideoFrameContainer container;
    container.push(makeVideoFrame(1.0, 0));
    container.push(makeVideoFrame(2.0, 0));

    auto frame = container.pop(1);
    当前 epoch 是 1，但帧都是 epoch 0 EXPECT_EQ(frame, nullptr);
}

TEST(VideoFrameContainerTest, SwitchToSorted) {
    VideoFrameContainer container;
    auto tf1 = makeVideoFrame(3.0, 0);
    pts 乱序 auto tf2 = makeVideoFrame(1.0, 0);
    auto tf3 = makeVideoFrame(2.0, 0);

    container.push(tf1);
    container.push(tf2);
    container.push(tf3);

    切换到 sorted map container.switchToSorted();

    pop 应该按 pts 顺序返回 auto frame = container.pop(0);
    ASSERT_TRUE(frame != nullptr);
    EXPECT_DOUBLE_EQ(frame->ptsTime(), 1.0);

    frame = container.pop(0);
    ASSERT_TRUE(frame != nullptr);
    EXPECT_DOUBLE_EQ(frame->ptsTime(), 2.0);

    frame = container.pop(0);
    ASSERT_TRUE(frame != nullptr);
    EXPECT_DOUBLE_EQ(frame->ptsTime(), 3.0);
}

TEST(VideoFrameContainerTest, SwitchToSortedIdempotent) {
    VideoFrameContainer container;
    container.push(makeVideoFrame(1.0, 0));
    container.switchToSorted();
    container.switchToSorted();
    第二次调用应该无操作
    EXPECT_EQ(container.size(), 1u);
}

TEST(VideoFrameContainerTest, BackPtsReturnsLastFramePts) {
    VideoFrameContainer container;
    container.push(makeVideoFrame(1.0, 0));
    container.push(makeVideoFrame(5.0, 0));
    container.push(makeVideoFrame(3.0, 0));

    auto backPts = container.backPts();
    ASSERT_TRUE(backPts.has_value());
    EXPECT_DOUBLE_EQ(backPts.value(), 3.0);
deque:
    last pushed
}

TEST(VideoFrameContainerTest, BackPtsWithSortedMap) {
    VideoFrameContainer container;
    container.push(makeVideoFrame(3.0, 0));
    container.push(makeVideoFrame(1.0, 0));
    container.push(makeVideoFrame(5.0, 0));
    container.switchToSorted();

    auto backPts = container.backPts();
    ASSERT_TRUE(backPts.has_value());
    EXPECT_DOUBLE_EQ(backPts.value(), 5.0);
    sorted map : highest pts
}

TEST(VideoFrameContainerTest, BackPtsReturnsNulloptWhenEmpty) {
    VideoFrameContainer container;
    EXPECT_EQ(container.backPts(), std::nullopt);
}

TEST(VideoFrameContainerTest, PopFrontRemovesOldestRegardlessOfEpoch) {
    VideoFrameContainer container;
    container.push(makeVideoFrame(1.0, 0));
    container.push(makeVideoFrame(2.0, 0));

    container.popFront();
    EXPECT_EQ(container.size(), 1u);

    auto frame = container.pop(0);
    ASSERT_TRUE(frame != nullptr);
    EXPECT_DOUBLE_EQ(frame->ptsTime(), 2.0);
}

TEST(VideoFrameContainerTest, ClearRemovesAllFrames) {
    VideoFrameContainer container;
    container.push(makeVideoFrame(1.0, 0));
    container.push(makeVideoFrame(2.0, 0));
    container.clear();
    EXPECT_TRUE(container.empty());
    EXPECT_EQ(container.size(), 0u);
}

MARK : -EpochFilter 测试

       TEST(EpochFilterTest, ShouldNotDiscardWhenNoSeekPending) {
    SeekCoordinator sc;
    没有 seek 时，帧不应该被丢弃 EXPECT_FALSE(EpochFilter::shouldDiscard(sc, 1.0));
}

TEST(EpochFilterTest, ShouldDiscardFrameBeforeSeekTarget) {
    SeekCoordinator sc;
    (void)sc.beginSeek(CTime(10.0));

    seek 目标 10s，帧 pts = 5s 应被丢弃 EXPECT_TRUE(EpochFilter::shouldDiscard(sc, 5.0));
}

TEST(EpochFilterTest, ShouldNotDiscardFrameAtOrAfterSeekTarget) {
    SeekCoordinator sc;
    (void)sc.beginSeek(CTime(10.0));

    seek 目标 10s，帧 pts =
        10s 或 15s 不应被丢弃 EXPECT_FALSE(EpochFilter::shouldDiscard(sc, 10.0));
    EXPECT_FALSE(EpochFilter::shouldDiscard(sc, 15.0));
}

TEST(EpochFilterTest, ShouldBreakWhenEpochAdvances) {
    SeekCoordinator sc;
    uint64_t capturedEpoch = sc.seekEpoch();
    0(void)sc.beginSeek(CTime(5.0));
    epoch → 1

        capturedEpoch 是旧 epoch，新的 seek 已发起 EXPECT_TRUE(
            EpochFilter::shouldBreak(sc, capturedEpoch));
}

TEST(EpochFilterTest, ShouldNotBreakWhenEpochMatches) {
    SeekCoordinator sc;
    (void)sc.beginSeek(CTime(5.0));
    uint64_t currentEpoch = sc.seekEpoch();
    1

        当前 epoch 匹配，没有新 seek EXPECT_FALSE(EpochFilter::shouldBreak(sc, currentEpoch));
}

TEST(EpochFilterTest, ShouldNotBreakWhenNoSeekPending) {
    SeekCoordinator sc;
    uint64_t epoch = sc.seekEpoch();
    0

        没有 seek pending，不需要 break EXPECT_FALSE(EpochFilter::shouldBreak(sc, epoch));
}

MARK : -PlayerStateMachine isValidTransition 补充测试

       TEST(PlayerStateMachineCoreTest, AllValidTransitions) {
    完整的状态转换矩阵验证

    struct Transition {
        PlayerState from;
        PlayerState to;
        bool expected;
    };

    std::vector<Transition> validTransitions = {
        {PlayerState::NotInited, PlayerState::Initializing, true},
        {PlayerState::Initializing, PlayerState::Prepared, true},
        {PlayerState::Initializing, PlayerState::Stop, true},
        {PlayerState::Initializing, PlayerState::Error, true},
        {PlayerState::Prepared, PlayerState::Buffering, true},
        {PlayerState::Prepared, PlayerState::Stop, true},
        {PlayerState::Prepared, PlayerState::Error, true},
        {PlayerState::Buffering, PlayerState::Ready, true},
        {PlayerState::Buffering, PlayerState::Playing, true},
        {PlayerState::Buffering, PlayerState::Pause, true},
        {PlayerState::Buffering, PlayerState::Stop, true},
        {PlayerState::Buffering, PlayerState::Error, true},
        {PlayerState::Ready, PlayerState::Playing, true},
        {PlayerState::Ready, PlayerState::Buffering, true},
        {PlayerState::Ready, PlayerState::Stop, true},
        {PlayerState::Ready, PlayerState::Error, true},
        {PlayerState::Playing, PlayerState::Pause, true},
        {PlayerState::Playing, PlayerState::Buffering, true},
        {PlayerState::Playing, PlayerState::Completed, true},
        {PlayerState::Playing, PlayerState::Stop, true},
        {PlayerState::Playing, PlayerState::Error, true},
        {PlayerState::Pause, PlayerState::Playing, true},
        {PlayerState::Pause, PlayerState::Buffering, true},
        {PlayerState::Pause, PlayerState::Stop, true},
        {PlayerState::Pause, PlayerState::Error, true},
        {PlayerState::Completed, PlayerState::Stop, true},
        {PlayerState::Error, PlayerState::Stop, true},
    };

    for (const auto& t : validTransitions) {
        EXPECT_EQ(PlayerStateMachine::isValidTransition(t.from, t.to), t.expected)
            << "Transition " << static_cast<int>(t.from) << " → " << static_cast<int>(t.to);
    }
}

TEST(PlayerStateMachineCoreTest, InvalidTransitions) {
    std::vector<std::pair<PlayerState, PlayerState>> invalidTransitions = {
        {PlayerState::NotInited, PlayerState::Prepared},
        {PlayerState::NotInited, PlayerState::Playing},
        {PlayerState::NotInited, PlayerState::Stop},
        {PlayerState::Stop, PlayerState::Playing},
        {PlayerState::Stop, PlayerState::Initializing},
        {PlayerState::Error, PlayerState::Playing},
        {PlayerState::Completed, PlayerState::Playing},
        {PlayerState::Completed, PlayerState::Pause},
    };

    for (const auto& [from, to] : invalidTransitions) {
        EXPECT_FALSE(PlayerStateMachine::isValidTransition(from, to))
            << "Invalid transition " << static_cast<int>(from) << " → " << static_cast<int>(to)
            << " should return false";
    }
}

MARK : -PipelineErrorBridge 测试
#include "pipeline/PipelineError.h"

       TEST(PipelineErrorBridgeTest, ErrorNetworkToPipelineError) {
    auto pipelineErr = toPipelineError(Error::NetworkError,
                                       PipelineEvent::ReadError,
                                       PipelineStage::Demux,
                                       "connection timeout");
    EXPECT_EQ(pipelineErr.event, PipelineEvent::ReadError);
    EXPECT_EQ(pipelineErr.stage, PipelineStage::Demux);
    EXPECT_EQ(pipelineErr.error, Error::NetworkError);
    EXPECT_EQ(pipelineErr.message, "connection timeout");
}

TEST(PipelineErrorBridgeTest, ErrorNotFoundToPipelineError) {
    auto pipelineErr = toPipelineError(Error::FileNotFound,
                                       PipelineEvent::ReadError,
                                       PipelineStage::Demux,
                                       "file not found");
    EXPECT_EQ(pipelineErr.error, Error::FileNotFound);
    EXPECT_EQ(pipelineErr.message, "file not found");
}

TEST(PipelineErrorBridgeTest, ErrorWithEmptyMessage) {
    auto pipelineErr =
        toPipelineError(Error::ConnectionTimeout, PipelineEvent::ReadError, PipelineStage::Demux);
    EXPECT_EQ(pipelineErr.error, Error::ConnectionTimeout);
    EXPECT_TRUE(pipelineErr.message.empty());
}

TEST(PipelineErrorBridgeTest, DecoderUnsupportedFormatToPipelineError) {
    auto pipelineErr = toPipelineError(Error::DecoderUnsupportedFormat,
                                       PipelineEvent::AudioDecoderFailed,
                                       PipelineStage::VideoDecode,
                                       "unsupported codec");
    EXPECT_EQ(pipelineErr.event, PipelineEvent::AudioDecoderFailed);
    EXPECT_EQ(pipelineErr.stage, PipelineStage::VideoDecode);
    EXPECT_EQ(pipelineErr.error, Error::DecoderUnsupportedFormat);
    EXPECT_EQ(pipelineErr.message, "unsupported codec");
}

TEST(PipelineErrorBridgeTest, DecoderSuccessToPipelineError) {
    auto pipelineErr = toPipelineError(Error::Success,
                                       PipelineEvent::VideoDecoderFailed,
                                       PipelineStage::VideoDecode);
    EXPECT_EQ(pipelineErr.error, Error::Success);
}

TEST(PipelineErrorBridgeTest, DecoderResourceExhaustedToPipelineError) {
    auto pipelineErr = toPipelineError(Error::DecoderResourceExhausted,
                                       PipelineEvent::VideoDecoderFailed,
                                       PipelineStage::VideoDecode);
    EXPECT_EQ(pipelineErr.error, Error::DecoderResourceExhausted);
}

TEST(PipelineErrorBridgeTest, DecoderConfigInvalidToPipelineError) {
    auto pipelineErr = toPipelineError(Error::DecoderConfigInvalid,
                                       PipelineEvent::AudioDecoderFailed,
                                       PipelineStage::VideoDecode);
    EXPECT_EQ(pipelineErr.error, Error::DecoderConfigInvalid);
}
