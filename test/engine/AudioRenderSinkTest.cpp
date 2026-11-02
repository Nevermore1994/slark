//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
// Desktop coverage for the production audio sink: ring-buffer accounting, position math,
// prefill, flush safety against the audio thread, and end-of-stream draining — all against
// a fake IAudioRender, no platform code involved.
//
#include "AudioRenderSink.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include "AudioInfo.h"

using namespace slark;
using namespace slark::engine;

namespace {

constexpr uint32_t kFrameBytes = 4096;  // ~21ms of 48kHz stereo s16

AVFrameRefPtr pcmFrame(uint8_t fill = 0) {
    auto frame = std::make_shared<AVFrame>(AVFrameType::Audio);
    frame->data = std::make_unique<Data>();
    frame->data->append(std::vector<uint8_t>(kFrameBytes, fill));
    return frame;
}

std::shared_ptr<AudioInfo> audioInfo() {
    auto info = std::make_shared<AudioInfo>();
    info->sampleRate = 48000;
    info->channels = 2;
    info->bitsPerSample = 16;
    return info;
}

struct FakeRender : public IAudioRender {
    explicit FakeRender(const std::shared_ptr<AudioInfo>& info)
        : IAudioRender(info), latency(Time::TimeDelta{static_cast<int64_t>(info->dataLen2TimePoint(9600).point())}) {}  // 50ms at 192000 B/s

    void play() noexcept override {
        ++plays;
    }

    void pause() noexcept override {
        ++pauses;
    }

    void stop() noexcept override {}
    void setVolume(float) noexcept override {}

    void flush() noexcept override {
        ++flushes;
    }

    void reset() noexcept override {}
    void renderEnd() noexcept override {}

    [[nodiscard]] Time::TimeDelta getHardwareLatency() noexcept override {
        return latency;
    }

    RequestAudioDataFunc grabProvider() {
        return provider_;
    }

    uint32_t pull(uint32_t bytes) {
        std::vector<uint8_t> buf(bytes, 0);
        AudioDataFlag flag = AudioDataFlag::Error;
        const auto n = requestAudioData(buf.data(), bytes, flag);
        if (flag == AudioDataFlag::Normal) {
            consumedFrames += n / kFrameBytes;
        }
        return n;
    }

    Time::TimeDelta latency;
    std::atomic<int> plays{0};
    std::atomic<int> pauses{0};
    std::atomic<int> flushes{0};
    std::atomic<int> consumedFrames{0};
};

struct SinkHarness {
    std::shared_ptr<FakeRender> render = std::make_shared<FakeRender>(audioInfo());
    std::unique_ptr<AudioRenderSink> sink;

    SinkHarness() {
        auto info = audioInfo();
        sink = std::make_unique<AudioRenderSink>(
            [this](const std::shared_ptr<AudioInfo>& i) {
                return render;
            },
            0.1);
        sink->open(info);
    }
};

}  // namespace

TEST(AudioRenderSinkTest, WritePrimesAndPositionFollowsConsumption) {
    SinkHarness h;
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(h.sink->write(pcmFrame()));
    }
    EXPECT_TRUE(h.sink->primed());
    EXPECT_EQ(h.render->plays.load(), 0);  // prefills while paused

    h.sink->start();
    EXPECT_EQ(h.render->plays.load(), 1);

    // Consume half; position = consumed − latency(50ms).
    for (int i = 0; i < 5; ++i) {
        h.render->pull(kFrameBytes);
    }
    const auto position = h.sink->position().second();
    EXPECT_NEAR(position, 5.0 * 0.021 - 0.05, 0.01);
    EXPECT_GE(position, 0.0);
}

TEST(AudioRenderSinkTest, PositionIsMonotonicAgainstLatencyJitter) {
    SinkHarness h;
    for (int i = 0; i < 10; ++i) {
        h.sink->write(pcmFrame());
    }
    double last = 0.0;
    for (int i = 0; i < 6; ++i) {
        h.render->pull(kFrameBytes);
        const auto position = h.sink->position().second();
        EXPECT_GE(position, last);
        last = position;
    }
}

TEST(AudioRenderSinkTest, FlushResetsBufferAndPosition) {
    SinkHarness h;
    for (int i = 0; i < 10; ++i) {
        h.sink->write(pcmFrame());
    }
    h.render->pull(kFrameBytes * 3);

    h.sink->flush(Time::TimePoint::fromSeconds(2.0));
    EXPECT_FALSE(h.sink->primed());
    EXPECT_DOUBLE_EQ(h.sink->position().second(), 2.0);
    EXPECT_EQ(h.render->flushes.load(), 1);

    // The decoder-ended flag was reset: pulling after flush reports an error again.
    AudioDataFlag flag = AudioDataFlag::Normal;
    std::vector<uint8_t> buf(64, 0);
    (void)h.render->requestAudioData(buf.data(), static_cast<uint32_t>(buf.size()), flag);
    EXPECT_EQ(flag, AudioDataFlag::Error);
}

TEST(AudioRenderSinkTest, DrainsOnlyAfterDecoderEndAndEmptyBuffer) {
    SinkHarness h;
    for (int i = 0; i < 4; ++i) {
        h.sink->write(pcmFrame());
    }
    h.sink->markDecoderEnded();
    EXPECT_FALSE(h.sink->drained());

    // Drain: the pull returns 0 once the buffer is empty (same contract as the legacy
    // render component), which is the signal that the hardware tail has been fed.
    while (h.render->pull(kFrameBytes) > 0) {
    }
    EXPECT_TRUE(h.sink->drained());
}

TEST(AudioRenderSinkTest, FullBufferRejectsWrites) {
    SinkHarness h;
    // 262144-byte ring; ~10ms frames: keep writing until one is rejected.
    bool rejected = false;
    for (int i = 0; i < 200 && !rejected; ++i) {
        rejected = !h.sink->write(pcmFrame());
    }
    EXPECT_TRUE(rejected);
}

TEST(AudioRenderSinkTest, SilencePaddingDoesNotAdvancePosition) {
    SinkHarness h;
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(h.sink->write(pcmFrame()));
    }
    h.sink->markDecoderEnded();
    EXPECT_EQ(h.render->pull(kFrameBytes * 20), kFrameBytes * 20);
    EXPECT_NEAR(h.sink->position().second(), 10 * 0.02133 - 0.05, 0.005);
}

TEST(AudioRenderSinkTest, DestroyingTheSinkDetachesTheProvider) {
    auto render = std::make_shared<FakeRender>(audioInfo());
    {
        AudioRenderSink sink([&](const std::shared_ptr<AudioInfo>&) { return render; }, 0.1);
        ASSERT_TRUE(sink.open(audioInfo()));
    }
    AudioDataFlag flag = AudioDataFlag::Normal;
    std::vector<uint8_t> buf(64, 0);
    EXPECT_EQ(render->requestAudioData(buf.data(), static_cast<uint32_t>(buf.size()), flag), 0u);
    EXPECT_EQ(flag, AudioDataFlag::Error);
}

TEST(AudioRenderSinkTest, FlushIsSafeWhileTheAudioThreadAndTheProducerRun) {
    SinkHarness h;
    std::atomic<bool> stop{false};
    std::atomic<int> torn{0};
    std::thread audio([&] {
        std::vector<uint8_t> buf(kFrameBytes);
        while (!stop) {
            AudioDataFlag flag = AudioDataFlag::Error;
            if (h.render->requestAudioData(buf.data(), kFrameBytes, flag) == 0) {
                continue;
            }
            if (std::any_of(buf.begin(), buf.end(), [&](uint8_t b) { return b != buf[0]; })) {
                ++torn;
            }
        }
    });
    std::thread producer([&] {
        uint8_t fill = 1;
        while (!stop) {
            if (h.sink->write(pcmFrame(fill))) {
                fill = static_cast<uint8_t>(fill % 250 + 1);
            } else {
                std::this_thread::yield();
            }
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    for (int i = 0; std::chrono::steady_clock::now() < deadline; ++i) {
        h.sink->flush(Time::TimePoint::fromSeconds(static_cast<double>(i)));
    }
    stop = true;
    audio.join();
    producer.join();
    EXPECT_EQ(torn.load(), 0);

    h.sink->flush(Time::TimePoint::fromSeconds(3.0));
    EXPECT_DOUBLE_EQ(h.sink->position().second(), 3.0);
    EXPECT_FALSE(h.sink->primed());
}

TEST(AudioRenderSinkTest, ACallbackThatOutlivesTheSinkIsHarmless) {
    auto render = std::make_shared<FakeRender>(audioInfo());
    RequestAudioDataFunc provider;
    {
        AudioRenderSink sink([&](const std::shared_ptr<AudioInfo>&) { return render; }, 0.1);
        ASSERT_TRUE(sink.open(audioInfo()));
        for (int i = 0; i < 4; ++i) {
            ASSERT_TRUE(sink.write(pcmFrame(7)));
        }
        provider = render->grabProvider();
    }
    ASSERT_TRUE(provider);
    std::vector<uint8_t> buf(kFrameBytes, 0);
    AudioDataFlag flag = AudioDataFlag::Normal;
    EXPECT_EQ(provider(buf.data(), kFrameBytes, flag), 0u);
    EXPECT_EQ(flag, AudioDataFlag::Error);
}

TEST(AudioRenderProviderTest, WithoutAProviderTheRequestFailsCleanly) {
    FakeRender render(audioInfo());
    std::vector<uint8_t> buf(64, 0);
    AudioDataFlag flag = AudioDataFlag::Normal;
    EXPECT_EQ(render.requestAudioData(buf.data(), static_cast<uint32_t>(buf.size()), flag), 0u);
    EXPECT_EQ(flag, AudioDataFlag::Error);
}

TEST(AudioRenderProviderTest, SetProviderWaitsForARunningCallback) {
    FakeRender render(audioInfo());
    std::promise<void> entered;
    std::promise<void> release;
    auto releaseSignal = release.get_future().share();
    render.setProvider([&](uint8_t*, uint32_t, AudioDataFlag& flag) {
        entered.set_value();
        releaseSignal.wait();
        flag = AudioDataFlag::Normal;
        return 7u;
    });

    uint32_t result = 0;
    std::thread audio([&] {
        std::vector<uint8_t> buf(16, 0);
        AudioDataFlag flag = AudioDataFlag::Error;
        result = render.requestAudioData(buf.data(), static_cast<uint32_t>(buf.size()), flag);
    });
    entered.get_future().wait();

    std::atomic<bool> replaced{false};
    std::thread control([&] {
        render.setProvider({});
        replaced = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(replaced.load());

    release.set_value();
    audio.join();
    control.join();
    EXPECT_TRUE(replaced.load());
    EXPECT_EQ(result, 7u);
}

TEST(AudioRenderProviderTest, ReplacingTheProviderWhileTheAudioThreadPullsIsSafe) {
    FakeRender render(audioInfo());
    std::atomic<bool> stop{false};
    std::atomic<int> unexpected{0};
    std::thread audio([&] {
        std::vector<uint8_t> buf(16, 0);
        const auto size = static_cast<uint32_t>(buf.size());
        while (!stop) {
            AudioDataFlag flag = AudioDataFlag::Normal;
            const auto n = render.requestAudioData(buf.data(), size, flag);
            const bool fromProvider = flag == AudioDataFlag::Normal && (n == 1 || n == 2);
            const bool noProvider = flag == AudioDataFlag::Error && n == 0;
            if (!fromProvider && !noProvider) {
                ++unexpected;
            }
        }
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    for (uint32_t i = 0; std::chrono::steady_clock::now() < deadline; ++i) {
        if (i % 4 == 3) {
            render.setProvider({});
            continue;
        }
        const uint32_t value = i % 2 + 1;
        render.setProvider([value](uint8_t*, uint32_t, AudioDataFlag& flag) {
            flag = AudioDataFlag::Normal;
            return value;
        });
    }
    stop = true;
    audio.join();
    EXPECT_EQ(unexpected.load(), 0);
}
