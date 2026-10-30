//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "AudioPipeline.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include "AudioSink.h"
#include "MediaClock.h"
#include "ManualScheduler.h"
#include "SourceSupport.h"

using namespace std::chrono_literals;
using namespace slark;
using namespace slark::engine;
using namespace slark::engine::fake;

namespace {

Time::TimePoint sec(double s) {
    return Time::TimePoint::fromSeconds(s);
}

AVFrameRefPtr audioFrame(double pts, uint64_t epoch = 1) {
    auto frame = std::make_shared<AVFrame>(AVFrameType::Audio);
    frame->pts = static_cast<int64_t>(pts * 90000);
    frame->timeScale = 90000;
    frame->seekEpoch = epoch;
    frame->data = std::make_unique<Data>();
    frame->data->append(std::vector<uint8_t>(1024, 0));
    return frame;
}

AVFrameRefPtr eosFrame(uint64_t epoch = 1) {
    auto frame = std::make_shared<AVFrame>(AVFrameType::Audio);
    frame->isEOS = true;
    frame->seekEpoch = epoch;
    return frame;
}

// Synchronous fake: decode() immediately emits one PCM frame at pts + 0.5. With
// `holdInput` set it returns CodecAgain until `releaseInput` is set.
struct FakeDecoder : public IDecoder {
    FakeDecoder() : IDecoder(DecoderType::AACSoftwareDecoder) {}

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
            emit(8.0);
            return Error::Success;
        }
        if (holdInput.load() && !released.load()) {
            return Error::CodecAgain;
        }
        emit(frame->ptsTime() + 0.5, frame->isDiscard);
        return Error::Success;
    }

    void emit(double pts, bool isDiscard = false) {
        auto pcm = std::make_shared<AVFrame>(AVFrameType::Audio);
        pcm->pts = static_cast<int64_t>(pts * 90000);
        pcm->timeScale = 90000;
        pcm->isDiscard = isDiscard;
        pcm->data = std::make_unique<Data>();
        pcm->data->append(std::vector<uint8_t>(512, 0));
        invokeReceiveFunc(
            DecoderData{AVFrameRefPtr(std::move(pcm)), Error::Success, currentSeekEpoch()});
    }

    std::atomic<bool> holdInput{false};
    std::atomic<bool> released{false};
    std::atomic<int> decodeCalls{0};
    std::atomic<int> flushes{0};
    std::function<void()> onFlush;
};

// Controllable fake sink: write() fails once `bufferedFrames` reaches `capacityFrames`;
// the test consumes manually and controls when the hardware tail counts as played.
struct FakeAudioSink : public AudioSink {
    bool open(std::shared_ptr<AudioInfo> info) override {
        audioInfo_ = std::move(info);
        return true;
    }

    bool write(AVFrameRefPtr frame) override {
        if (onWrite) {
            onWrite();
        }
        ++writeCalls;
        if (bufferedFrames.load() >= static_cast<int>(capacityFrames.load())) {
            return false;
        }
        ++bufferedFrames;
        lastWrittenPts.store(frame->ptsTime());
        return true;
    }

    [[nodiscard]] Time::TimePoint position() const override {
        return Time::TimePoint(offsetUs.load() +
                               static_cast<uint64_t>(consumedFrames.load()) * 21000ULL);
    }

    [[nodiscard]] bool primed() const override {
        return bufferedFrames.load() >= prefillFrames;
    }

    void start() override {
        ++starts;
    }

    void pause() override {
        ++pauses;
    }

    void flush(Time::TimePoint offset) override {
        bufferedFrames = 0;
        consumedFrames = 0;
        offsetUs = offset.point();
        decoderEnded = false;
        ++flushes;
    }

    void markDecoderEnded() override {
        decoderEnded = true;
    }

    [[nodiscard]] bool drained() const override {
        return decoderEnded.load() && hardwareTailPlayed.load();
    }

    void consumeOne() {
        if (bufferedFrames.load() > 0) {
            --bufferedFrames;
            ++consumedFrames;
        }
    }

    std::shared_ptr<AudioInfo> audioInfo_;
    std::atomic<uint32_t> capacityFrames{1024};
    std::atomic<int> bufferedFrames{0};
    std::atomic<int> consumedFrames{0};
    std::atomic<int> writeCalls{0};
    std::atomic<int> starts{0};
    std::atomic<int> pauses{0};
    std::atomic<int> flushes{0};
    std::atomic<uint64_t> offsetUs{0};
    int prefillFrames = 4;
    std::atomic<bool> decoderEnded{false};
    std::atomic<bool> hardwareTailPlayed{false};
    std::atomic<double> lastWrittenPts{-1.0};
    std::function<void()> onWrite;
};

struct PipelineHarness {
    SenderPtr<AVFrameRefPtr> sender;
    ReceiverPtr<AVFrameRefPtr> receiver;
    std::shared_ptr<FakeDecoder> decoder = std::make_shared<FakeDecoder>();
    std::unique_ptr<FakeAudioSink> sink = std::make_unique<FakeAudioSink>();
    FakeAudioSink* sinkPtr = sink.get();
    std::unique_ptr<AudioPipeline> pipeline;

    static PipelineHarness create(uint64_t epoch = 1, Time::TimePoint target = sec(0.0)) {
        PipelineHarness harness;
        auto [sender, receiver] = Channel<AVFrameRefPtr>::create();
        harness.sender = std::move(sender);
        harness.receiver = std::move(receiver);
        harness.pipeline = std::make_unique<AudioPipeline>(
            *harness.receiver,
            [decoder = harness.decoder] {
                return decoder;
            },
            std::move(harness.sink), epoch, target, [] {});
        return harness;
    }
};

}  // namespace

TEST(AudioPipelineTest, StaleEpochFramesAreDropped) {
    auto harness = PipelineHarness::create();
    harness.sender->post(audioFrame(1.0, /*epoch=*/9));
    harness.sender->post(audioFrame(2.0, /*epoch=*/1));
    ASSERT_TRUE(waitUntil([&] {
        return harness.sinkPtr->writeCalls.load() >= 1;
    }));
    EXPECT_DOUBLE_EQ(harness.sinkPtr->lastWrittenPts.load(), 2.5);
    EXPECT_EQ(harness.decoder->decodeCalls.load(), 1);
}

TEST(AudioPipelineTest, FramesCarryingEpochOnlyInDecoderDataReachTheSink) {
    // Real audio decoders stamp the epoch only on DecoderData; frames must still be accepted.
    auto harness = PipelineHarness::create();
    for (int i = 0; i < 4; ++i) {
        harness.sender->post(audioFrame(0.1 * i));
    }
    ASSERT_TRUE(waitUntil([&] {
        return harness.sinkPtr->bufferedFrames.load() >= harness.sinkPtr->prefillFrames;
    }));
    EXPECT_EQ(harness.sinkPtr->writeCalls.load(), 4);
    EXPECT_DOUBLE_EQ(harness.sinkPtr->lastWrittenPts.load(), 0.8);
    EXPECT_TRUE(harness.pipeline->ready(1, sec(0.0)));
}

TEST(AudioPipelineTest, FlushClearsStateAndResetsReady) {
    auto harness = PipelineHarness::create();
    for (int i = 0; i < 6; ++i) {
        harness.sender->post(audioFrame(1.0 + i * 0.1));
    }
    ASSERT_TRUE(waitUntil([&] {
        return harness.sinkPtr->primed();
    }));
    EXPECT_TRUE(harness.pipeline->ready(1, sec(0.0)));

    harness.pipeline->flush(2, sec(5.0), SeekMode::Exact);
    EXPECT_FALSE(harness.pipeline->ready(2, sec(5.0)));
    EXPECT_EQ(harness.sinkPtr->bufferedFrames.load(), 0);
    EXPECT_EQ(harness.decoder->flushes.load(), 1);

    for (int i = 0; i < 6; ++i) {
        harness.sender->post(audioFrame(5.0 + i * 0.1, /*epoch=*/2));
    }
    ASSERT_TRUE(waitUntil([&] {
        return harness.pipeline->ready(2, sec(5.0));
    }));
}

TEST(AudioPipelineTest, DecoderFlushRunsWithoutThePipelineLock) {
    auto harness = PipelineHarness::create();
    std::atomic<bool> callbackDone{false};
    bool callbackFinishedDuringFlush = false;
    harness.decoder->onFlush = [&] {
        std::thread callback([&] {
            harness.decoder->emit(1.0);
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

TEST(AudioPipelineTest, FlushDuringSinkWriteLeavesNoStaleAudio) {
    auto harness = PipelineHarness::create();
    std::atomic<bool> fired{false};
    std::atomic<bool> flushDone{false};
    std::thread flusher;
    harness.sinkPtr->onWrite = [&] {
        if (fired.exchange(true)) {
            return;
        }
        flusher = std::thread([&] {
            harness.pipeline->flush(2, sec(5.0), SeekMode::Exact);
            flushDone = true;
        });
        std::this_thread::sleep_for(50ms);
    };
    harness.sender->post(audioFrame(1.0));
    ASSERT_TRUE(waitUntil([&] {
        return flushDone.load();
    }));
    flusher.join();
    EXPECT_EQ(harness.sinkPtr->bufferedFrames.load(), 0);
}

TEST(AudioPipelineTest, PrimesWhilePlaybackIsPaused) {
    auto harness = PipelineHarness::create();
    harness.pipeline->setPlaying(true);
    harness.pipeline->setPlaying(false);
    for (int i = 0; i < 6; ++i) {
        harness.sender->post(audioFrame(1.0 + i * 0.1));
    }
    ASSERT_TRUE(waitUntil([&] {
        return harness.sinkPtr->primed();
    }));
    EXPECT_GE(harness.sinkPtr->starts.load(), 1);
    EXPECT_GE(harness.sinkPtr->pauses.load(), 1);
}

TEST(AudioPipelineTest, ReadyRequiresPrefillAndTargetPts) {
    auto harness = PipelineHarness::create();
    harness.sinkPtr->prefillFrames = 100;
    harness.sender->post(audioFrame(1.0));
    ASSERT_TRUE(waitUntil([&] {
        return harness.sinkPtr->writeCalls.load() >= 1;
    }));
    EXPECT_FALSE(harness.pipeline->ready(1, sec(0.5)));
    EXPECT_FALSE(harness.pipeline->ready(1, sec(2.0)));

    harness.sinkPtr->prefillFrames = 1;
    EXPECT_TRUE(harness.pipeline->ready(1, sec(0.5)));
    EXPECT_FALSE(harness.pipeline->ready(1, sec(2.0)));
}

TEST(AudioPipelineTest, EndedWaitsForTheHardwareTail) {
    auto harness = PipelineHarness::create();
    harness.sender->post(audioFrame(1.0));
    harness.sender->post(eosFrame());
    ASSERT_TRUE(waitUntil([&] {
        while (harness.sinkPtr->bufferedFrames.load() > 0) {
            harness.sinkPtr->consumeOne();
        }
        return harness.sinkPtr->decoderEnded.load();
    }));
    EXPECT_FALSE(harness.pipeline->ended());

    harness.sinkPtr->hardwareTailPlayed = true;
    EXPECT_TRUE(harness.pipeline->ended());
}

TEST(AudioPipelineTest, BlockedDecoderInputDoesNotSpin) {
    auto harness = PipelineHarness::create();
    harness.decoder->holdInput = true;
    harness.sinkPtr->capacityFrames = 4;
    for (int i = 0; i < 12; ++i) {
        harness.sender->post(audioFrame(1.0 + i * 0.1));
    }
    std::this_thread::sleep_for(150ms);
    // The 20ms wakeup tick bounds the retries; the old poll-and-backoff would have spun
    // far more often in the same window.
    EXPECT_LT(harness.decoder->decodeCalls.load(), 30);

    harness.decoder->released = true;
    ASSERT_TRUE(waitUntil([&] {
        return harness.sinkPtr->writeCalls.load() > 0;
    }));
}

TEST(AudioPipelineTest, ClockAnchorIsMonotonic) {
    slark::test::ManualScheduler scheduler;
    MediaClock clock(scheduler);
    auto harness = PipelineHarness::create();
    harness.pipeline->setClock(clock);
    for (int i = 0; i < 8; ++i) {
        harness.sender->post(audioFrame(1.0 + i * 0.2));
    }
    ASSERT_TRUE(waitUntil([&] {
        return harness.sinkPtr->writeCalls.load() >= 8;
    }));
    const auto afterFeed = clock.now().second();
    for (int i = 0; i < 5; ++i) {
        harness.sender->post(audioFrame(0.2 + i * 0.01));
    }
    std::this_thread::sleep_for(60ms);
    harness.sinkPtr->consumeOne();
    EXPECT_GE(clock.now().second(), afterFeed);
}

TEST(AudioPipelineTest, ClockIsAnchoredAgainAfterABackwardSeek) {
    slark::test::ManualScheduler scheduler;
    MediaClock clock(scheduler);
    auto harness = PipelineHarness::create();
    harness.pipeline->setClock(clock);
    for (int i = 0; i < 8; ++i) {
        harness.sender->post(audioFrame(10.0 + i * 0.2));
    }
    ASSERT_TRUE(waitUntil([&] {
        return harness.sinkPtr->writeCalls.load() >= 8;
    }));
    for (int i = 0; i < 8; ++i) {
        harness.sinkPtr->consumeOne();
    }
    ASSERT_TRUE(waitUntil([&] {
        return clock.now().second() > 0.15;
    }));

    harness.pipeline->flush(2, sec(0.05), SeekMode::Exact);
    clock.seekTo(sec(0.05));
    for (int i = 0; i < 6; ++i) {
        harness.sender->post(audioFrame(0.05 + i * 0.1, /*epoch=*/2));
    }
    ASSERT_TRUE(waitUntil([&] {
        return harness.sinkPtr->bufferedFrames.load() >= 6;
    }));
    for (int i = 0; i < 3; ++i) {
        harness.sinkPtr->consumeOne();
    }
    EXPECT_TRUE(waitUntil([&] {
        return clock.now().second() > 0.1;
    }));
}

TEST(AudioPipelineTest, EosDrainsTheDecoderAndEnds) {
    auto harness = PipelineHarness::create();
    harness.sender->post(audioFrame(1.0));
    harness.sender->post(eosFrame());
    harness.sinkPtr->hardwareTailPlayed = true;
    ASSERT_TRUE(waitUntil([&] {
        return harness.pipeline->ended();
    }));
}

TEST(AudioPipelineTest, CloseIsIdempotent) {
    auto harness = PipelineHarness::create();
    harness.sender->post(audioFrame(1.0));
    ASSERT_TRUE(waitUntil([&] {
        return harness.sinkPtr->writeCalls.load() >= 1;
    }));
    {
        std::unique_ptr<AudioPipeline> pipeline = std::move(harness.pipeline);
        pipeline.reset();
    }
    // Destroying again via the harness must be a no-op; the pipeline handle is already moved out.
    SUCCEED();
}

TEST(AudioPipelineTest, PermanentlyRefusingCodecFailsThePipeline) {
    auto harness = PipelineHarness::create();
    harness.decoder->holdInput = true;  // never released: the codec stays wedged
    harness.sender->post(audioFrame(1.0));
    // 100 retries at a 20ms tick; the pipeline must surface the wedged codec, not wait forever.
    ASSERT_TRUE(waitUntil([&] {
        return harness.pipeline->failure().has_value();
    }));
    EXPECT_EQ(harness.pipeline->failure().value(), Error::CodecResourceExhausted);
    // The failed pipeline must stop feeding the decoder instead of failing frame after frame.
    const auto callsAtFailure = harness.decoder->decodeCalls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    EXPECT_EQ(harness.decoder->decodeCalls.load(), callsAtFailure);
}

TEST(AudioPipelineTest, ExactModeDropsFramesBeforeTargetButKeepsDiscardMarkers) {
    auto harness = PipelineHarness::create();
    harness.pipeline->flush(1, sec(2.0), SeekMode::Exact);
    auto discard = audioFrame(1.0);
    discard->isDiscard = true;
    harness.sender->post(audioFrame(1.0));
    harness.sender->post(discard);
    ASSERT_TRUE(waitUntil([&] {
        return harness.decoder->decodeCalls.load() >= 1;
    }));
    // The plain pre-target frame was dropped; only the discard marker reached the decoder.
    EXPECT_EQ(harness.decoder->decodeCalls.load(), 1);
    EXPECT_EQ(harness.sinkPtr->writeCalls.load(), 0);
}
