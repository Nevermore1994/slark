//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "pipeline/BufferMonitor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace slark;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::Return;

// MARK: - Mock delegate

class MockDelegate : public IBufferMonitorDelegate {
public:
    MOCK_METHOD(double, demuxedDuration, (), (const, noexcept, override));
    MOCK_METHOD(void, pauseStream, (), (noexcept, override));
    MOCK_METHOD(void, resumeStream, (), (noexcept, override));
    MOCK_METHOD(int32_t, audioDecodedFrameCount, (), (const, noexcept, override));
    MOCK_METHOD(int32_t, videoDecodedFrameCount, (), (const, noexcept, override));
    MOCK_METHOD(void, pauseAudioDecoder, (), (noexcept, override));
    MOCK_METHOD(void, resumeAudioDecoder, (), (noexcept, override));
    MOCK_METHOD(void, pauseVideoDecoder, (), (noexcept, override));
    MOCK_METHOD(void, resumeVideoDecoder, (), (noexcept, override));
    MOCK_METHOD(void, onPrebufferComplete, (), (noexcept, override));
    MOCK_METHOD(void, onDropOldest, (), (noexcept, override));
    MOCK_METHOD(void,
                onBufferEvent,
                (PipelineEvent, std::optional<PipelineError>),
                (noexcept, override));
};

// MARK: - 预缓冲

TEST(BufferMonitorTest, StartPrebufferingFiresImmediatelyWhenStartBufferIsZero) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forLocal();
    cfg.startBufferDuration = 0.0;

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, demuxedDuration()).WillRepeatedly(Return(0.0));
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    EXPECT_FALSE(monitor.isPrebuffering());
}

TEST(BufferMonitorTest, StartPrebufferingWaitsUntilDurationReached) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();
    // startBufferDuration = 2.0

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, demuxedDuration())
        .WillOnce(Return(1.0))        // 未达到
        .WillRepeatedly(Return(3.0)); // 达到
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    EXPECT_TRUE(monitor.isPrebuffering());

    monitor.onDemuxedDataEnqueued(); // 1.0 → 未完成
    EXPECT_TRUE(monitor.isPrebuffering());

    // 绕过节流：直接调用 heartbeat
    monitor.onHeartbeat(); // 3.0 → 完成
    EXPECT_FALSE(monitor.isPrebuffering());
}

// MARK: - 高水位线背压

TEST(BufferMonitorTest, PausesStreamWhenDurationExceedsHighWatermark) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();
    // demuxHighWatermarkDuration = 10.0

    EXPECT_CALL(delegate, demuxedDuration()).WillRepeatedly(Return(12.0));
    EXPECT_CALL(delegate, pauseStream()).Times(AtLeast(1));
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.onHeartbeat();
}

TEST(BufferMonitorTest, ResumesStreamWhenDurationDropsBelowLowWatermark) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();
    // demuxLowWatermarkDuration = 3.0

    // 先触发 pause，再触发 resume
    EXPECT_CALL(delegate, demuxedDuration())
        .WillOnce(Return(12.0))       // pause
        .WillRepeatedly(Return(1.0)); // resume
    EXPECT_CALL(delegate, pauseStream()).Times(1);
    EXPECT_CALL(delegate, resumeStream()).Times(1);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.onHeartbeat(); // pause
    monitor.onHeartbeat(); // resume
}

// MARK: - 重缓冲

TEST(BufferMonitorTest, RebufferingStartsWhenStreamRunningAndDurationDropsBelowLow) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();
    // startBufferDuration = 2.0, rebufferScale = 1.5 → rebufferThreshold = 3.0

    // 场景：prebuffer 完成后，stream 正在运行（未 pause），duration 降到 rebufferThreshold 以下 → BufferingStart
    EXPECT_CALL(delegate, demuxedDuration())
        .WillOnce(Return(3.0))        // heartbeat 1: prebuffer complete
        .WillRepeatedly(Return(1.0)); // heartbeat 2+: low watermark，stream 未 pause
    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, testing::Eq(std::nullopt)))
        .Times(1);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    monitor.onHeartbeat(); // prebuffer complete
    monitor.onHeartbeat(); // duration=1.0，stream 未 pause → BufferingStart
    EXPECT_TRUE(monitor.isRebuffering());
}

TEST(BufferMonitorTest, RebufferingEndsWhenStreamResumesAndDurationRecovered) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();
    // startBufferDuration = 2.0, rebufferScale = 1.5 → rebufferThreshold = 3.0

    // 场景：高水位 pause → 低水位 resume → checkRebufferComplete 触发 BufferingEnd
    // 注意：BufferingStart 由 onStarvation 触发（绕过 streamPaused_ 约束）
    EXPECT_CALL(delegate, demuxedDuration())
        .WillOnce(Return(3.0))  // heartbeat 1: prebuffer complete
        .WillOnce(Return(12.0)) // heartbeat 2: high watermark → pause
        .WillRepeatedly(
            Return(3.0)); // heartbeat 3: low watermark，streamPaused_=true → resume + BufferingEnd
    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, pauseStream()).Times(1);
    EXPECT_CALL(delegate, resumeStream()).Times(1);
    // 通过 onStarvation 触发 BufferingStart（不依赖 streamPaused_ 状态）
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, testing::Eq(std::nullopt)))
        .Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingEnd, testing::Eq(std::nullopt)))
        .Times(1);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    monitor.onHeartbeat(); // prebuffer complete
    monitor.onHeartbeat(); // pause
    // 手动触发 rebuffering 状态（模拟 starvation 路径）
    monitor.onStarvation(false);
    monitor.onStarvation(false);
    monitor.onStarvation(false);
    monitor.onStarvation(false);
    monitor.onStarvation(false); // 第 5 次 → BufferingStart
    EXPECT_TRUE(monitor.isRebuffering());
    monitor.onHeartbeat(); // duration=3.0，streamPaused_=true → resumeStream +
                           // checkRebufferComplete → BufferingEnd
    EXPECT_FALSE(monitor.isRebuffering());
}

// MARK: - 解码器水位线

TEST(BufferMonitorTest, PausesAudioDecoderAtHighWatermark) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();
    // audioDecoderHighWatermarkFrames = 30

    EXPECT_CALL(delegate, demuxedDuration()).WillRepeatedly(Return(5.0));
    EXPECT_CALL(delegate, audioDecodedFrameCount())
        .WillRepeatedly(Return(cfg.audioDecoderHighWatermarkFrames));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, pauseAudioDecoder()).Times(AtLeast(1));

    BufferMonitor monitor(cfg, delegate);
    monitor.onHeartbeat();
}

TEST(BufferMonitorTest, ResumesAudioDecoderAtLowWatermark) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    EXPECT_CALL(delegate, demuxedDuration()).WillRepeatedly(Return(5.0));
    EXPECT_CALL(delegate, audioDecodedFrameCount())
        .WillOnce(Return(cfg.audioDecoderHighWatermarkFrames))       // pause
        .WillRepeatedly(Return(cfg.audioDecoderLowWatermarkFrames)); // resume
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, pauseAudioDecoder()).Times(1);
    EXPECT_CALL(delegate, resumeAudioDecoder()).Times(1);

    BufferMonitor monitor(cfg, delegate);
    monitor.onHeartbeat(); // pause
    monitor.onHeartbeat(); // resume
}

TEST(BufferMonitorTest, PausesVideoDecoderAtHighWatermark) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    EXPECT_CALL(delegate, demuxedDuration()).WillRepeatedly(Return(5.0));
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount())
        .WillRepeatedly(Return(cfg.videoDecoderHighWatermarkFrames));
    EXPECT_CALL(delegate, pauseVideoDecoder()).Times(AtLeast(1));

    BufferMonitor monitor(cfg, delegate);
    monitor.onHeartbeat();
}

// MARK: - EOF 停止背压

TEST(BufferMonitorTest, DemuxEofSkipsDataFlowRegulation) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    EXPECT_CALL(delegate, demuxedDuration()).Times(0);
    EXPECT_CALL(delegate, pauseStream()).Times(0);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.setDemuxEof();
    monitor.onHeartbeat();
}

TEST(BufferMonitorTest, DecoderEofSkipsDecoderFlowRegulation) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    EXPECT_CALL(delegate, demuxedDuration()).WillRepeatedly(Return(5.0));
    EXPECT_CALL(delegate, audioDecodedFrameCount()).Times(0);
    EXPECT_CALL(delegate, videoDecodedFrameCount()).Times(0);

    BufferMonitor monitor(cfg, delegate);
    monitor.setDecoderEof();
    monitor.onHeartbeat();
}

// MARK: - 背压暂停（快速推帧）

TEST(BufferMonitorTest, BackpressurePausedSkipsDataFlowRegulation) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    // pauseBackpressure 仅在 streamPaused_ = true 时才调用 resumeStream
    // 初始状态 streamPaused_ = false，所以不会调用 resumeStream
    // 背压暂停后 regulateDataFlow 跳过，不应调用 pauseStream/onDropOldest
    EXPECT_CALL(delegate, demuxedDuration()).WillRepeatedly(Return(25.0));
    EXPECT_CALL(delegate, resumeStream()).Times(0); // 初始未 pause，不会调用
    EXPECT_CALL(delegate, pauseStream()).Times(0);  // 背压暂停期间跳过
    EXPECT_CALL(delegate, onDropOldest()).Times(0);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.pauseBackpressure();
    monitor.onHeartbeat();
}

TEST(BufferMonitorTest, BackpressurePausedResumesStreamWhenPreviouslyPaused) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    // 先触发 pause，再 pauseBackpressure → 此时 streamPaused_ = true → 调用 resumeStream
    EXPECT_CALL(delegate, demuxedDuration())
        .WillOnce(Return(12.0))        // 第一次 heartbeat：高水位 → pause
        .WillRepeatedly(Return(25.0)); // 之后：背压暂停，跳过
    EXPECT_CALL(delegate, pauseStream()).Times(1);
    EXPECT_CALL(delegate, resumeStream()).Times(1); // pauseBackpressure 触发
    EXPECT_CALL(delegate, onDropOldest()).Times(0);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.onHeartbeat();       // pause
    monitor.pauseBackpressure(); // resumeStream
    monitor.onHeartbeat();       // 背压暂停，跳过
}

// MARK: - reset

TEST(BufferMonitorTest, ResetClearsAllState) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    EXPECT_CALL(delegate, demuxedDuration()).WillRepeatedly(Return(3.0));
    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    monitor.onHeartbeat(); // complete prebuffer

    monitor.reset();
    EXPECT_FALSE(monitor.isPrebuffering());
    EXPECT_FALSE(monitor.isRebuffering());
}

// MARK: - 饥饿检测

TEST(BufferMonitorTest, StarvationTriggersRebufferingAfterThreshold) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    EXPECT_CALL(delegate, demuxedDuration()).WillRepeatedly(Return(3.0));
    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, testing::Eq(std::nullopt)))
        .Times(1);

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    monitor.onHeartbeat(); // prebuffer complete

    // kStarvationThreshold = 5
    for (int32_t i = 0; i < 5; ++i) {
        monitor.onStarvation(false);
    }
    EXPECT_TRUE(monitor.isRebuffering());
}

TEST(BufferMonitorTest, StarvationIgnoredForLocalSource) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forLocal();

    EXPECT_CALL(delegate, demuxedDuration()).WillRepeatedly(Return(0.0));
    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, onBufferEvent(_, _)).Times(0);

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering(); // startBufferDuration = 0 → immediate

    for (int32_t i = 0; i < 10; ++i) {
        monitor.onStarvation(true);
    }
    EXPECT_FALSE(monitor.isRebuffering());
}
