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

    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, demuxedDuration())
        .WillOnce(Return(1.0))
        .WillRepeatedly(Return(3.0));
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    EXPECT_TRUE(monitor.isPrebuffering());

    monitor.onDemuxedDataEnqueued();
    EXPECT_TRUE(monitor.isPrebuffering());

    monitor.onHeartbeat();
    EXPECT_FALSE(monitor.isPrebuffering());
}


TEST(BufferMonitorTest, PausesStreamWhenDurationExceedsHighWatermark) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    EXPECT_CALL(delegate, demuxedDuration())
        .WillOnce(Return(0.0))
        .WillOnce(Return(3.0))
        .WillRepeatedly(Return(12.0));
    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, pauseStream()).Times(AtLeast(1));
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    monitor.onHeartbeat();
    monitor.onHeartbeat();
    monitor.onHeartbeat();
}

TEST(BufferMonitorTest, ResumesStreamWhenDurationDropsBelowLowWatermark) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    EXPECT_CALL(delegate, demuxedDuration())
        .WillOnce(Return(0.0))
        .WillOnce(Return(3.0))
        .WillOnce(Return(12.0))
        .WillRepeatedly(Return(1.0));
    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, pauseStream()).Times(1);
    EXPECT_CALL(delegate, resumeStream()).Times(1);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    monitor.onHeartbeat();
    monitor.onHeartbeat();
    monitor.onHeartbeat();
    monitor.onHeartbeat();
}


TEST(BufferMonitorTest, RebufferingStartsWhenStreamRunningAndDurationDropsBelowLow) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    EXPECT_CALL(delegate, demuxedDuration())
        .WillOnce(Return(3.0))
        .WillRepeatedly(Return(1.0));
    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, testing::Eq(std::nullopt)))
        .Times(1);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    monitor.onHeartbeat();
    monitor.onHeartbeat();
    EXPECT_TRUE(monitor.isRebuffering());
}

TEST(BufferMonitorTest, RebufferingEndsOnHeartbeatWhenStreamNeverPaused) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    EXPECT_CALL(delegate, demuxedDuration())
        .WillOnce(Return(3.0))
        .WillOnce(Return(3.0))
        .WillOnce(Return(4.0))
        .WillOnce(Return(4.0))
        .WillOnce(Return(4.0))
        .WillRepeatedly(Return(4.0));
    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, pauseStream()).Times(0);
    EXPECT_CALL(delegate, resumeStream()).Times(0);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, testing::Eq(std::nullopt)))
        .Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingEnd, testing::Eq(std::nullopt)))
        .Times(1);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    monitor.onHeartbeat();
    monitor.onStarvation(false);
    monitor.onStarvation(false);
    monitor.onStarvation(false);
    monitor.onStarvation(false);
    monitor.onStarvation(false);
    EXPECT_TRUE(monitor.isRebuffering());
    monitor.onHeartbeat();
    EXPECT_FALSE(monitor.isRebuffering());
}

TEST(BufferMonitorTest, RebufferingEndsWhenStreamResumesAndDurationRecovered) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    EXPECT_CALL(delegate, demuxedDuration())
        .WillOnce(Return(3.0))
        .WillOnce(Return(12.0))
        .WillRepeatedly(Return(3.0));
    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, pauseStream()).Times(1);
    EXPECT_CALL(delegate, resumeStream()).Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingStart, testing::Eq(std::nullopt)))
        .Times(1);
    EXPECT_CALL(delegate, onBufferEvent(PipelineEvent::BufferingEnd, testing::Eq(std::nullopt)))
        .Times(1);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    monitor.onHeartbeat();
    monitor.onHeartbeat();
    monitor.onStarvation(false);
    monitor.onStarvation(false);
    monitor.onStarvation(false);
    monitor.onStarvation(false);
    monitor.onStarvation(false);
    EXPECT_TRUE(monitor.isRebuffering());
    monitor.onHeartbeat();
    EXPECT_FALSE(monitor.isRebuffering());
}


TEST(BufferMonitorTest, PausesAudioDecoderAtHighWatermark) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

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
        .WillOnce(Return(cfg.audioDecoderHighWatermarkFrames))
        .WillRepeatedly(Return(cfg.audioDecoderLowWatermarkFrames));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, pauseAudioDecoder()).Times(1);
    EXPECT_CALL(delegate, resumeAudioDecoder()).Times(1);

    BufferMonitor monitor(cfg, delegate);
    monitor.onHeartbeat();
    monitor.onHeartbeat();
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


TEST(BufferMonitorTest, BackpressurePausedSkipsDataFlowRegulation) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    EXPECT_CALL(delegate, demuxedDuration()).WillRepeatedly(Return(25.0));
    EXPECT_CALL(delegate, resumeStream()).Times(0);
    EXPECT_CALL(delegate, pauseStream()).Times(0);
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

    EXPECT_CALL(delegate, demuxedDuration())
        .WillOnce(Return(0.0))
        .WillOnce(Return(3.0))
        .WillOnce(Return(12.0))
        .WillRepeatedly(Return(25.0));
    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, pauseStream()).Times(1);
    EXPECT_CALL(delegate, resumeStream()).Times(1);
    EXPECT_CALL(delegate, onDropOldest()).Times(0);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    monitor.onHeartbeat();
    monitor.onHeartbeat();
    monitor.onHeartbeat();
    monitor.pauseBackpressure();
    monitor.onHeartbeat();
}


TEST(BufferMonitorTest, ResetClearsAllState) {
    MockDelegate delegate;
    auto cfg = BackpressureConfig::forNetwork();

    EXPECT_CALL(delegate, demuxedDuration()).WillRepeatedly(Return(3.0));
    EXPECT_CALL(delegate, onPrebufferComplete()).Times(1);
    EXPECT_CALL(delegate, audioDecodedFrameCount()).WillRepeatedly(Return(0));
    EXPECT_CALL(delegate, videoDecodedFrameCount()).WillRepeatedly(Return(0));

    BufferMonitor monitor(cfg, delegate);
    monitor.startPrebuffering();
    monitor.onHeartbeat();

    monitor.reset();
    EXPECT_FALSE(monitor.isPrebuffering());
    EXPECT_FALSE(monitor.isRebuffering());
}


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
    monitor.onHeartbeat();

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
    monitor.startPrebuffering();

    for (int32_t i = 0; i < 10; ++i) {
        monitor.onStarvation(true);
    }
    EXPECT_FALSE(monitor.isRebuffering());
}
