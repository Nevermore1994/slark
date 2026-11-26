//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
// Epoch discipline shared by both pipelines: stale frames are dropped at the entry, and a
// generation bump on one track leaves the other untouched.
//
#include <gtest/gtest.h>

#include <atomic>

#include "AudioPipeline.h"
#include "AudioSink.h"
#include "SourceSupport.h"
#include "VideoPipeline.h"
#include "VideoSink.h"

using namespace std::chrono_literals;
using namespace slark;
using namespace slark::engine;
using namespace slark::engine::fake;

namespace {

Time::TimePoint sec(double s) {
    return Time::TimePoint::fromSeconds(s);
}

AVFrameRefPtr mediaFrame(AVFrameType type, double pts, uint64_t epoch, bool keyframe = false) {
    auto frame = std::make_shared<AVFrame>(type);
    frame->pts = static_cast<int64_t>(pts * 90000);
    frame->timeScale = 90000;
    frame->seekEpoch = epoch;
    if (type == AVFrameType::Audio) {
        frame->data = std::make_unique<Data>();
        frame->data->append(std::vector<uint8_t>(1024, 0));
    } else {
        auto info = std::make_shared<VideoFrameInfo>();
        info->frameType = keyframe ? VideoFrameType::IFrame : VideoFrameType::Unknown;
        frame->info = std::move(info);
        frame->data = std::make_unique<Data>();
        frame->data->append(std::vector<uint8_t>(64, 0));
    }
    return frame;
}

struct CountingAudioSink : public AudioSink {
    bool open(std::shared_ptr<AudioInfo> info) override {
        audioInfo_ = std::move(info);
        return true;
    }

    bool write(AVFrameRefPtr frame) override {
        (void)frame;
        ++writes;
        return true;
    }

    [[nodiscard]] Time::TimePoint position() const override {
        return Time::TimePoint{static_cast<uint64_t>(consumed.load()) * 21000ULL};
    }

    [[nodiscard]] bool primed() const override {
        return writes >= 1;
    }

    [[nodiscard]] double bufferedSec() const override {
        return 0.0;
    }

    void start() override {}

    void pause() override {}

    void flush(Time::TimePoint offset) override {
        (void)offset;
        consumed = 0;
    }

    void markDecoderEnded() override {
        decoderEnded = true;
    }

    [[nodiscard]] bool drained() const override {
        return decoderEnded;
    }

    std::shared_ptr<AudioInfo> audioInfo_;
    std::atomic<int> writes{0};
    std::atomic<int> consumed{0};
    std::atomic<bool> decoderEnded{false};
};

AVFrameRefPtr eosFrame(AVFrameType type, uint64_t epoch) {
    auto frame = std::make_shared<AVFrame>(type);
    frame->isEOS = true;
    frame->seekEpoch = epoch;
    return frame;
}

struct CountingVideoSink : public VideoSink {
    void present(AVFrameRefPtr frame) override {
        (void)frame;
        ++presents;
    }

    void dropFrames(int32_t count) override {
        (void)count;
    }

    [[nodiscard]] int32_t heldCount() const override {
        return 0;
    }

    std::atomic<int> presents{0};
};

// Both fakes mirror the input epoch on their output, so a write/present proves a frame of that
// generation travelled the whole pipeline.
struct EchoAudioDecoder : public IDecoder {
    EchoAudioDecoder() : IDecoder(DecoderType::AACSoftwareDecoder) {}

    bool open(std::shared_ptr<DecoderConfig>) noexcept override {
        isOpen_ = true;
        return true;
    }

    void flush() noexcept override {}

    Error decode(AVFrameRefPtr& frame) noexcept override {
        auto out = std::make_shared<AVFrame>(AVFrameType::Audio);
        out->pts = frame->pts;
        out->timeScale = frame->timeScale;
        out->seekEpoch = frame->seekEpoch;
        out->data = std::make_unique<Data>();
        out->data->append(std::vector<uint8_t>(512, 0));
        invokeReceiveFunc(DecoderData{AVFrameRefPtr(std::move(out))});
        return Error::Success;
    }
};

struct EchoVideoDecoder : public IDecoder {
    EchoVideoDecoder() : IDecoder(DecoderType::VideoHardwareDecoder) {}

    bool open(std::shared_ptr<DecoderConfig>) noexcept override {
        isOpen_ = true;
        return true;
    }

    void flush() noexcept override {}

    Error decode(AVFrameRefPtr& frame) noexcept override {
        auto out = std::make_shared<AVFrame>(AVFrameType::Video);
        out->pts = frame->pts;
        out->timeScale = frame->timeScale;
        out->seekEpoch = frame->seekEpoch;
        out->info = decodedVideoInfo();
        out->data = std::make_unique<Data>();
        out->data->append(std::vector<uint8_t>(32, 0));
        invokeReceiveFunc(DecoderData{AVFrameRefPtr(std::move(out))});
        return Error::Success;
    }
};

}  // namespace

TEST(PipelineEpochTest, StaleFramesNeverReachTheSinkOnEitherPipeline) {
    auto [audioTx, audioRx] = Channel<AVFrameRefPtr>::create();
    auto [videoTx, videoRx] = Channel<AVFrameRefPtr>::create();
    (void)audioRx;
    (void)videoRx;

    auto audioSink = std::make_unique<CountingAudioSink>();
    auto* audioSinkPtr = audioSink.get();
    auto videoSink = std::make_unique<CountingVideoSink>();
    auto* videoSinkPtr = videoSink.get();

    AudioPipeline audioPipeline(
        *audioRx,
        [] {
            return std::make_shared<EchoAudioDecoder>();
        },
        std::move(audioSink), 1, sec(0.0), [] {});
    VideoPipeline videoPipeline(
        *videoRx,
        [] {
            return std::make_shared<EchoVideoDecoder>();
        },
        std::move(videoSink), VideoPipeline::Config{}, 1, sec(0.0), [] {});

    // epoch 9 is stale on both pipelines and must vanish at the entry.
    audioTx->post(mediaFrame(AVFrameType::Audio, 1.0, /*epoch=*/9));
    videoTx->post(mediaFrame(AVFrameType::Video, 1.0, /*epoch=*/9));
    audioTx->post(mediaFrame(AVFrameType::Audio, 1.0, /*epoch=*/1));
    videoTx->post(mediaFrame(AVFrameType::Video, 1.0, /*epoch=*/1, /*keyframe=*/true));

    ASSERT_TRUE(waitUntil([&] {
        videoPipeline.onVsync(sec(0.0));
        return audioSinkPtr->writes.load() >= 1 && videoSinkPtr->presents.load() >= 1;
    }));
    audioTx->post(eosFrame(AVFrameType::Audio, 1));
    videoTx->post(eosFrame(AVFrameType::Video, 1));
    ASSERT_TRUE(waitUntil([&] {
        return audioPipeline.ended() && videoPipeline.ended();
    }));
    EXPECT_EQ(audioSinkPtr->writes.load(), 1);
    EXPECT_EQ(videoSinkPtr->presents.load(), 1);
}

TEST(PipelineEpochTest, AudioEpochBumpDoesNotDisturbVideo) {
    auto [audioTx, audioRx] = Channel<AVFrameRefPtr>::create();
    auto [videoTx, videoRx] = Channel<AVFrameRefPtr>::create();
    (void)audioRx;
    (void)videoRx;

    auto audioSink = std::make_unique<CountingAudioSink>();
    auto* audioSinkPtr = audioSink.get();
    auto videoSink = std::make_unique<CountingVideoSink>();
    auto* videoSinkPtr = videoSink.get();

    AudioPipeline audioPipeline(
        *audioRx,
        [] {
            return std::make_shared<EchoAudioDecoder>();
        },
        std::move(audioSink), 1, sec(0.0), [] {});
    VideoPipeline videoPipeline(
        *videoRx,
        [] {
            return std::make_shared<EchoVideoDecoder>();
        },
        std::move(videoSink), VideoPipeline::Config{}, 1, sec(0.0), [] {});
    videoPipeline.setPlaying(true);

    // Audio track switch: only audio flushes to epoch 2; video stays on epoch 1 and keeps
    // accepting its frames.
    audioPipeline.flush(2, sec(0.0), SeekMode::Exact);
    audioTx->post(mediaFrame(AVFrameType::Audio, 1.0, /*epoch=*/2));
    videoTx->post(mediaFrame(AVFrameType::Video, 1.0, /*epoch=*/1, /*keyframe=*/true));

    ASSERT_TRUE(waitUntil([&] {
        videoPipeline.onVsync(sec(0.0));
        return audioSinkPtr->writes.load() >= 1 && videoSinkPtr->presents.load() >= 1;
    }));

    audioTx->post(mediaFrame(AVFrameType::Audio, 2.0, /*epoch=*/2));
    videoTx->post(mediaFrame(AVFrameType::Video, 2.0, /*epoch=*/1, /*keyframe=*/true));
    ASSERT_TRUE(waitUntil([&] {
        videoPipeline.onVsync(sec(2.0));
        return audioSinkPtr->writes.load() >= 2 && videoSinkPtr->presents.load() >= 2;
    }));
}
