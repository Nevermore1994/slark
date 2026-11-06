//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "VideoPipeline.h"

#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <thread>

#include "SourceSupport.h"
#include "ThreadManager.h"
#include "VideoSink.h"

using namespace std::chrono_literals;
using namespace slark;
using namespace slark::engine;
using namespace slark::engine::fake;

namespace {

Time::TimePoint sec(double s) {
    return Time::TimePoint::fromSeconds(s);
}

AVFrameRefPtr videoFrame(double pts, uint64_t epoch = 1, bool keyframe = false) {
    auto frame = std::make_shared<AVFrame>(AVFrameType::Video);
    frame->pts = static_cast<int64_t>(pts * 90000);
    frame->timeScale = 90000;
    frame->seekEpoch = epoch;
    auto info = std::make_shared<VideoFrameInfo>();
    info->frameType = keyframe ? VideoFrameType::IFrame : VideoFrameType::Unknown;
    frame->info = std::move(info);
    frame->data = std::make_unique<Data>();
    frame->data->append(std::vector<uint8_t>(64, 0));
    return frame;
}

AVFrameRefPtr eosFrame(uint64_t epoch = 1) {
    auto frame = std::make_shared<AVFrame>(AVFrameType::Video);
    frame->isEOS = true;
    frame->seekEpoch = epoch;
    return frame;
}

// Synchronous fake: decode(frame) emits one queueable frame at pts + 0.5; EOS completes.
struct FakeVideoDecoder : public IDecoder {
    FakeVideoDecoder() : IDecoder(DecoderType::VideoHardwareDecoder) {}

    bool open(std::shared_ptr<DecoderConfig>) noexcept override {
        isOpen_ = true;
        return true;
    }

    void flush() noexcept override {
        if (onFlush) {
            onFlush();
        }
        ++flushes;
    }

    Error decode(AVFrameRefPtr& frame) noexcept override {
        ++decodeCalls;
        if (frame->isEOS) {
            isCompleted_ = true;
            return Error::Success;
        }
        if (holdInput.load()) {
            return Error::CodecAgain;
        }
        lastSeenDiscard = frame->isDiscard;
        // Model a codec whose output buffers are still checked out: it refuses new input
        // until renderOutput() takes them, exactly like Texture-mode MediaCodec.
        const auto limit = maxCheckedOutOutput.load();
        if (limit > 0) {
            {
                std::lock_guard lock(pendingMutex_);
                if (static_cast<int>(pendingOutput_.size()) >= limit) {
                    return Error::CodecAgain;
                }
            }
            auto pending = std::make_shared<AVFrame>(AVFrameType::Video);
            pending->pts = frame->pts;
            pending->timeScale = frame->timeScale;
            pending->seekEpoch =
                epochOnlyInData.load() ? 0 : currentSeekEpoch();
            pending->isDiscard = frame->isDiscard;
            pending->info = keepsFrameType.load() ? frame->info : decodedVideoInfo();
            pending->data = std::make_unique<Data>();
            pending->data->append(std::vector<uint8_t>(32, 0));
            std::lock_guard lock(pendingMutex_);
            pendingOutput_.push_back(std::move(pending));
            return Error::Success;
        }
        auto out = std::make_shared<AVFrame>(AVFrameType::Video);
        out->pts = frame->pts;
        out->timeScale = frame->timeScale;
        out->seekEpoch = epochOnlyInData.load() ? 0 : currentSeekEpoch();
        out->isDiscard = frame->isDiscard;
        out->info = keepsFrameType.load() ? frame->info : decodedVideoInfo();
        out->data = std::make_unique<Data>();
        out->data->append(std::vector<uint8_t>(32, 0));
        invokeReceiveFunc(DecoderData{AVFrameRefPtr(std::move(out)), Error::Success,
                                       currentSeekEpoch()});
        return Error::Success;
    }

    bool hasPendingOutput() const noexcept override {
        std::lock_guard lock(pendingMutex_);
        return !pendingOutput_.empty();
    }

    Error renderOutput() noexcept override {
        AVFrameRefPtr frame;
        {
            std::lock_guard lock(pendingMutex_);
            if (pendingOutput_.empty()) {
                return Error::CodecAgain;
            }
            frame = std::move(pendingOutput_.front());
            pendingOutput_.pop_front();
        }
        ++renderCalls;
        invokeReceiveFunc(DecoderData{std::move(frame), Error::Success, currentSeekEpoch()});
        return Error::Success;
    }

    std::atomic<int> decodeCalls{0};
    std::atomic<int> flushes{0};
    std::atomic<int> renderCalls{0};
    std::atomic<bool> lastSeenDiscard{false};
    std::atomic<bool> holdInput{false};  // when true, decode() always reports a full queue
    // When > 0, decoded frames stay checked out until renderOutput() takes them and decode()
    // reports a full queue while the limit is reached.
    std::atomic<int> maxCheckedOutOutput{0};
    // When true, outputs carry their epoch only in DecoderData (the audio path's old
    // behaviour); the pipeline must still accept them.
    std::atomic<bool> epochOnlyInData{false};
    // Real decoders drop the frame type; only the live catch-up probe reads it from the output.
    std::atomic<bool> keepsFrameType{false};
    mutable std::mutex pendingMutex_;
    std::deque<AVFrameRefPtr> pendingOutput_;
    std::function<void()> onFlush;
};

struct FakeVideoSink : public VideoSink {
    void present(AVFrameRefPtr frame) override {
        presentedPts.push_back(frame->ptsTime());
        ++held;
    }

    void dropFrames(int32_t count) override {
        drops += count;
    }

    [[nodiscard]] int32_t heldCount() const override {
        return held.load();
    }

    std::vector<double> presentedPts;
    std::atomic<int32_t> held{0};
    std::atomic<int32_t> drops{0};
};

struct PipelineHarness {
    std::shared_ptr<std::atomic<int>> wakes = std::make_shared<std::atomic<int>>(0);
    SenderPtr<AVFrameRefPtr> sender;
    ReceiverPtr<AVFrameRefPtr> receiver;
    std::shared_ptr<FakeVideoDecoder> decoder = std::make_shared<FakeVideoDecoder>();
    std::shared_ptr<FakeVideoSink> sink = std::make_shared<FakeVideoSink>();
    std::unique_ptr<VideoPipeline> pipeline;

    static PipelineHarness create(uint64_t epoch = 1,
                              Time::TimePoint target = sec(0.0),
                              VideoPipeline::Config config = {}) {
        PipelineHarness harness;
        auto [sender, receiver] = Channel<AVFrameRefPtr>::create();
        harness.sender = std::move(sender);
        harness.receiver = std::move(receiver);
        harness.pipeline = std::make_unique<VideoPipeline>(
            *harness.receiver,
            [decoder = harness.decoder] {
                return decoder;
            },
            harness.sink, config, epoch, target, [wakes = harness.wakes] {
                ++*wakes;
            });
        return harness;
    }
};

}  // namespace

TEST(VideoPipelineTest, TheWorkerIsARegisteredThreadThatLeavesWithThePipeline) {
    auto& threads = ThreadManager::shareInstance();
    const auto before = threads.size();
    auto harness = PipelineHarness::create();
    EXPECT_EQ(threads.size(), before + 1);
    harness.pipeline.reset();
    EXPECT_EQ(threads.size(), before);
}

TEST(VideoPipelineTest, AnIdlePipelineShutsDownPromptly) {
    auto harness = PipelineHarness::create();
    std::this_thread::sleep_for(50ms);
    const auto start = std::chrono::steady_clock::now();
    harness.pipeline.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - start, 500ms);
}

TEST(VideoPipelineTest, FirstFramePresentsEvenWhenAheadIsBelowStartBuffer) {
    auto harness = PipelineHarness::create(1, sec(0.0));
    harness.pipeline->flush(1, sec(0.0), SeekMode::Fast);
    harness.sender->post(videoFrame(0.0, 1, /*keyframe=*/true));
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(sec(0.0));
        return !harness.sink->presentedPts.empty();
    }));
    EXPECT_DOUBLE_EQ(harness.sink->presentedPts.front(), 0.0);
}

TEST(VideoPipelineTest, ExactModeDropsPreTargetFramesAndPresentsTheTargetFrame) {
    auto harness = PipelineHarness::create(1, sec(2.0));
    for (int i = 0; i < 4; ++i) {
        harness.sender->post(videoFrame(1.0 + i * 0.2));
    }
    harness.sender->post(videoFrame(2.0));
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(sec(2.0));
        return !harness.sink->presentedPts.empty();
    }));
    EXPECT_DOUBLE_EQ(harness.sink->presentedPts.front(), 2.0);
    // Pre-target frames went into the decoder but never onto the screen.
    EXPECT_GE(harness.decoder->decodeCalls.load(), 5);
}

TEST(VideoPipelineTest, DecoderFlushRunsWithoutThePipelineLock) {
    auto harness = PipelineHarness::create();
    std::atomic<bool> callbackDone{false};
    bool callbackFinishedDuringFlush = false;
    harness.decoder->onFlush = [&] {
        std::thread callback([&] {
            auto out = std::make_shared<AVFrame>(AVFrameType::Video);
            out->data = std::make_unique<Data>();
            out->data->append(std::vector<uint8_t>(8, 0));
            harness.decoder->invokeReceiveFunc(
                DecoderData{AVFrameRefPtr(std::move(out)), Error::Success, 1});
            callbackDone = true;
        });
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!callbackDone && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(1ms);
        }
        callbackFinishedDuringFlush = callbackDone.load();
        callback.join();
    };
    harness.pipeline->flush(2, sec(5.0), SeekMode::Exact);
    EXPECT_TRUE(callbackFinishedDuringFlush);
}

TEST(VideoPipelineTest, PrerollAndStarvationWakeTheEngine) {
    VideoPipeline::Config config;
    config.starvationThreshold = 2;
    auto harness = PipelineHarness::create(1, sec(0.0), config);
    harness.pipeline->setPlaying(true);
    harness.sender->post(videoFrame(0.0, 1, true));
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(sec(0.0));
        return harness.pipeline->ready(1, sec(0.0));
    }));
    const auto afterPreroll = harness.wakes->load();
    EXPECT_GE(afterPreroll, 1);

    harness.pipeline->onVsync(sec(5.0));
    harness.pipeline->onVsync(sec(5.0));
    harness.pipeline->onVsync(sec(5.0));
    EXPECT_FALSE(harness.pipeline->ready(1, sec(0.0)));
    EXPECT_GT(harness.wakes->load(), afterPreroll);
}

TEST(VideoPipelineTest, ReadyTracksTheCurrentEpochOnly) {
    auto harness = PipelineHarness::create();
    harness.sender->post(videoFrame(0.0, 1, true));
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(sec(0.0));
        return harness.pipeline->ready(1, sec(0.0));
    }));
    EXPECT_FALSE(harness.pipeline->ready(2, sec(0.0)));
}

TEST(VideoPipelineTest, StarvedVsyncsClearReady) {
    VideoPipeline::Config config;
    config.starvationThreshold = 3;
    auto harness = PipelineHarness::create(1, sec(0.0), config);
    // Starvation only matters while playing; a paused player may legitimately hold nothing.
    harness.pipeline->setPlaying(true);
    harness.sender->post(videoFrame(0.0, 1, true));
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(sec(0.0));
        return harness.pipeline->ready(1, sec(0.0));
    }));

    // No source data follows: the queue empties and consecutive starved vsyncs clear ready.
    for (int i = 0; i < 5; ++i) {
        harness.pipeline->onVsync(sec(0.0));
    }
    EXPECT_FALSE(harness.pipeline->ready(1, sec(0.0)));

    // Pausing clears the starvation verdict and stops counting empty vsyncs.
    harness.pipeline->setPlaying(false);
    EXPECT_TRUE(harness.pipeline->ready(1, sec(0.0)));
}

TEST(VideoPipelineTest, RenderHeldFramesCountTowardsTheQueueLimit) {
    VideoPipeline::Config config;
    config.maxQueueFrames = 2;
    auto harness = PipelineHarness::create(1, sec(0.0), config);
    for (int i = 0; i < 3; ++i) {
        harness.sender->post(videoFrame(0.2 * i, 1, i == 0));
    }
    // Present the keyframe and the next frame; both stay held by the render side.
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(
            sec(0.2 * static_cast<double>(harness.sink->presentedPts.size())));
        return harness.sink->presentedPts.size() >= 2;
    }));
    EXPECT_EQ(harness.sink->heldCount(), 2);

    // Queue capacity is maxQueueFrames(2) + held(2): with four slots full the pipeline stops
    // feeding the decoder even though more frames keep arriving.
    const auto beforeSettle = harness.decoder->decodeCalls.load();
    for (int i = 0; i < 8; ++i) {
        harness.sender->post(videoFrame(1.0 + i * 0.1));
    }
    std::this_thread::sleep_for(120ms);
    const auto settled = harness.decoder->decodeCalls.load();
    std::this_thread::sleep_for(80ms);
    EXPECT_EQ(harness.decoder->decodeCalls.load(), settled);
    EXPECT_LE(settled - beforeSettle, 5);
}

TEST(VideoPipelineTest, AFullPresentationQueueResumesWhenVsyncOpensRoom) {
    VideoPipeline::Config config;
    config.maxQueueFrames = 2;
    auto harness = PipelineHarness::create(1, sec(0.0), config);
    for (int i = 0; i < 3; ++i) {
        harness.sender->post(videoFrame(0.2 * i, 1, i == 0));
    }
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(
            sec(0.2 * static_cast<double>(harness.sink->presentedPts.size())));
        return harness.sink->presentedPts.size() >= 2;
    }));
    // Stall the queue: the render side holds everything and the input keeps arriving.
    for (int i = 0; i < 6; ++i) {
        harness.sender->post(videoFrame(1.0 + i * 0.1));
    }
    std::this_thread::sleep_for(100ms);
    const auto blocked = harness.decoder->decodeCalls.load();

    // The clock reaching the queued frames presents one and opens a slot; the decoder must resume
    // well before the 20ms retry tick, so the wakeup has to come from onVsync itself.
    const auto opened = std::chrono::steady_clock::now();
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(sec(0.5));
        return harness.decoder->decodeCalls.load() > blocked;
    }));
    EXPECT_LT(std::chrono::steady_clock::now() - opened, 15ms);
}

TEST(VideoPipelineTest, EosEndsThePipelineOnceTheQueueDrains) {
    auto harness = PipelineHarness::create();
    harness.sender->post(eosFrame());
    ASSERT_TRUE(waitUntil([&] {
        return harness.pipeline->ended();
    }));
    EXPECT_TRUE(harness.pipeline->ready(1, sec(0.0)));
}

TEST(VideoPipelineTest, FlushResetsPrerollAndEpoch) {
    auto harness = PipelineHarness::create();
    harness.sender->post(videoFrame(0.0, 1, true));
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(sec(0.0));
        return harness.pipeline->ready(1, sec(0.0));
    }));

    harness.pipeline->flush(2, sec(1.0), SeekMode::Exact);
    EXPECT_FALSE(harness.pipeline->ready(2, sec(1.0)));
    EXPECT_EQ(harness.decoder->flushes.load(), 1);

    harness.sender->post(videoFrame(1.0, 2));
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(sec(1.0));
        return harness.pipeline->ready(2, sec(1.0));
    }));
    EXPECT_DOUBLE_EQ(harness.sink->presentedPts.back(), 1.0);
}

// flush() cannot reach an EOS that is already in the pipeline's own inbox.
TEST(VideoPipelineTest, AnEndOfStreamFromBeforeTheFlushDoesNotEndTheNewEpoch) {
    auto harness = PipelineHarness::create(1, sec(0.0));
    harness.decoder->holdInput = true;
    harness.sender->post(videoFrame(5.0, 1, /*keyframe=*/true));
    harness.sender->post(eosFrame(1));
    // The frame sticks in the pipeline because the codec refuses it; the EOS waits behind it.
    ASSERT_TRUE(waitUntil([&] {
        return harness.decoder->decodeCalls.load() > 2;
    }));

    harness.pipeline->flush(2, sec(1.0), SeekMode::Fast);
    harness.decoder->holdInput = false;
    std::this_thread::sleep_for(100ms);
    harness.pipeline->onVsync(sec(1.0));

    EXPECT_FALSE(harness.pipeline->ended());
    EXPECT_FALSE(harness.pipeline->ready(2, sec(1.0)));

    harness.sender->post(videoFrame(1.0, 2, /*keyframe=*/true));
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(sec(1.0));
        return harness.pipeline->ready(2, sec(1.0));
    }));
    EXPECT_FALSE(harness.pipeline->ended());
}

TEST(VideoPipelineTest, FramesCarryingEpochOnlyInDecoderDataArePresented) {
    auto harness = PipelineHarness::create(1, sec(0.0));
    harness.decoder->epochOnlyInData = true;
    harness.pipeline->flush(1, sec(0.0), SeekMode::Fast);
    harness.sender->post(videoFrame(0.0, 1, /*keyframe=*/true));
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(sec(0.0));
        return !harness.sink->presentedPts.empty();
    }));
    EXPECT_DOUBLE_EQ(harness.sink->presentedPts.front(), 0.0);
}

TEST(VideoPipelineTest, KeepsDrainingOutputsSoAFullCodecQueueRecovers) {
    auto harness = PipelineHarness::create(1, sec(0.0));
    harness.decoder->maxCheckedOutOutput = 2;
    harness.pipeline->flush(1, sec(0.0), SeekMode::Fast);
    for (int i = 0; i < 6; ++i) {
        harness.sender->post(videoFrame(0.1 * i, 1, i == 0));
    }
    // Texture-mode MediaCodec refuses input while outputs are checked out; the pipeline must drain.
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(sec(0.0));
        return harness.decoder->renderCalls.load() >= 6;
    }));
    EXPECT_FALSE(harness.pipeline->failure().has_value());
    EXPECT_EQ(harness.decoder->decodeCalls.load(), 6);
    EXPECT_EQ(harness.decoder->renderCalls.load(), 6);
    // A moving clock then presents them in order.
    for (int i = 0; i < 40 && harness.sink->presentedPts.size() < 6; ++i) {
        harness.pipeline->onVsync(sec(0.05 * i));
    }
    EXPECT_GE(harness.sink->presentedPts.size(), 6u);
}

TEST(VideoPipelineTest, PermanentlyRefusingCodecFailsThePipeline) {
    auto harness = PipelineHarness::create();
    harness.decoder->holdInput = true;  // never released: the codec stays wedged
    harness.sender->post(videoFrame(0.0, 1, true));
    ASSERT_TRUE(waitUntil([&] {
        return harness.pipeline->failure().has_value();
    }));
    EXPECT_EQ(harness.pipeline->failure().value(), Error::CodecResourceExhausted);
    // The failed pipeline must stop feeding the decoder instead of failing frame after frame.
    const auto callsAtFailure = harness.decoder->decodeCalls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    EXPECT_EQ(harness.decoder->decodeCalls.load(), callsAtFailure);
}

TEST(VideoPipelineTest, LiveCatchUpDrainsToTheNextKeyframe) {
    auto harness = PipelineHarness::create();
    harness.decoder->keepsFrameType = true;
    harness.pipeline->setCatchUpProbe([] {
        return true;
    });
    for (int i = 0; i < 6; ++i) {
        harness.sender->post(videoFrame(1.0 + i * 0.1));
    }
    harness.sender->post(videoFrame(2.0, 1, /*keyframe=*/true));
    // Let the pipeline decode everything (no vsync yet): the catch-up tick then drains the queue
    // to the keyframe before any preroll can present a stale frame.
    ASSERT_TRUE(waitUntil([&] {
        return harness.decoder->decodeCalls.load() >= 7;
    }));
    // Wait for a catch-up pass that ran *after* every frame was queued: an earlier pass or
    // an early vsync would preroll a stale frame and make the assertion timing-dependent.
    const auto catchUpsBefore = harness.pipeline->catchUpCount();
    ASSERT_TRUE(waitUntil([&] {
        return harness.pipeline->catchUpCount() > catchUpsBefore;
    }));
    ASSERT_TRUE(waitUntil([&] {
        harness.pipeline->onVsync(sec(3.0));
        return !harness.sink->presentedPts.empty();
    }));
    // The catch-up drained everything before the keyframe, so the keyframe became the preview.
    EXPECT_DOUBLE_EQ(harness.sink->presentedPts.front(), 2.0);
}
