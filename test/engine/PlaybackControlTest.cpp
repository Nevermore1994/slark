//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
// Closed-loop checks for the playback commands (play/pause/seek/loop) driven through the
// real engine, sources and demuxers, with scripted decoders and sinks.
//
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>
#include <algorithm>

#include "AudioSink.h"
#include "Command.h"
#include "ComponentFactories.h"
#include "PlaybackFactory.h"
#include "Engine.h"
#include "LogOutput.h"
#include "Notifier.h"
#include "PlayerContext.h"
#include "Scheduler.h"
#include "SourceSupport.h"
#include "ThreadManager.h"
#include "VideoSink.h"

using namespace std::chrono_literals;
using namespace slark;
using namespace slark::engine;

namespace {

constexpr const char* kWavPath = "../../test_sample/sample-3s.wav";
constexpr const char* kMp4Path = "../../test_sample/Sample.mp4";

Time::TimePoint sec(double s) {
    return Time::TimePoint::fromSeconds(s);
}

// Bounded sink: reports position only for consumed frames, so the pipeline paces like a device.
struct GatedAudioSink {
    bool write(AVFrameRefPtr) {
        if (buffered.load() >= capacity) {
            return false;
        }
        ++buffered;
        return true;
    }
    [[nodiscard]] Time::TimePoint position() const {
        constexpr int64_t kFrameUs = 6000;
        return Time::TimePoint(baseUs.load() +
                               static_cast<uint64_t>(consumed.load()) * kFrameUs);
    }
    [[nodiscard]] bool primed() const {
        return buffered.load() >= 4;
    }
    void flush(Time::TimePoint offset) {
        baseUs.store(offset.point());
        consumed.store(0);
        buffered.store(0);
        ended.store(false);
    }
    void consume(int count = 1) {
        for (int i = 0; i < count; ++i) {
            if (buffered.load() <= 0) {
                return;
            }
            --buffered;
            ++consumed;
        }
    }
    void markDecoderEnded() {
        ended.store(true);
    }
    [[nodiscard]] bool drained() const {
        return ended.load() && buffered.load() == 0;
    }

    [[nodiscard]] double bufferedSec() const {
        return buffered.load() * 0.02;
    }

    int capacity = 8;
    std::atomic<int> buffered{0};
    std::atomic<int> consumed{0};
    std::atomic<std::int64_t> baseUs{0};
    std::atomic<bool> ended{false};
};

struct SharedAudioSink : public AudioSink {
    explicit SharedAudioSink(std::shared_ptr<GatedAudioSink> inner) : inner_(std::move(inner)) {}

    bool open(std::shared_ptr<AudioInfo>) override {
        return true;
    }
    bool write(AVFrameRefPtr frame) override {
        return inner_->write(std::move(frame));
    }
    [[nodiscard]] Time::TimePoint position() const override {
        return inner_->position();
    }
    [[nodiscard]] bool primed() const override {
        return inner_->primed();
    }

    [[nodiscard]] double bufferedSec() const override {
        return inner_->bufferedSec();
    }
    void start() override {}
    void pause() override {}
    void flush(Time::TimePoint offset) override {
        inner_->flush(offset);
    }
    void markDecoderEnded() override {
        inner_->markDecoderEnded();
    }
    [[nodiscard]] bool drained() const override {
        return inner_->drained();
    }

    std::shared_ptr<GatedAudioSink> inner_;
};

struct CountingVideoSink : public VideoSink {
    void present(AVFrameRefPtr) override {
        ++presented;
    }
    void dropFrames(int32_t) override {}
    [[nodiscard]] int32_t heldCount() const override {
        return 0;
    }

    std::atomic<int> presented{0};
};

// Audio fake: emits one PCM frame per input, stamping the epoch only through DecoderData
// (matching the platform audio decoder).
struct EchoAudioDecoder : public IDecoder {
    EchoAudioDecoder() : IDecoder(DecoderType::RAW) {}

    bool open(std::shared_ptr<DecoderConfig>) noexcept override {
        isOpen_ = true;
        return true;
    }
    void flush() noexcept override {
        ++flushes;
    }
    Error decode(AVFrameRefPtr& frame) noexcept override {
        if (frame && frame->isEOS) {
            isCompleted_ = true;
            return Error::Success;
        }
        auto out = std::make_shared<AVFrame>(AVFrameType::Audio);
        out->pts = frame->pts;
        out->timeScale = frame->timeScale;
        out->data = std::make_unique<Data>();
        out->data->append(std::vector<uint8_t>(512, 0));
        invokeReceiveFunc(DecoderData{AVFrameRefPtr(std::move(out)), Error::Success,
                                       currentSeekEpoch()});
        return Error::Success;
    }

    std::atomic<int> flushes{0};
};

struct EchoVideoDecoder : public IDecoder {
    EchoVideoDecoder() : IDecoder(DecoderType::VideoHardwareDecoder) {}

    bool open(std::shared_ptr<DecoderConfig>) noexcept override {
        isOpen_ = true;
        return true;
    }
    void flush() noexcept override {
        ++flushes;
    }
    Error decode(AVFrameRefPtr& frame) noexcept override {
        if (!frame) {
            return Error::CodecError;
        }
        if (frame->isEOS) {
            isCompleted_ = true;
            return Error::Success;
        }
        auto out = std::make_shared<AVFrame>(AVFrameType::Video);
        out->pts = frame->pts;
        out->timeScale = frame->timeScale;
        out->info = fake::decodedVideoInfo();
        out->data = std::make_unique<Data>();
        out->data->append(std::vector<uint8_t>(64, 0));
        invokeReceiveFunc(DecoderData{AVFrameRefPtr(std::move(out)), Error::Success,
                                       currentSeekEpoch()});
        return Error::Success;
    }

    std::atomic<int> flushes{0};
};

struct Harness {
    std::unique_ptr<ThreadScheduler> scheduler;
    std::unique_ptr<Notifier> notifier;
    std::shared_ptr<Engine> engine;
    std::shared_ptr<GatedAudioSink> audioSink = std::make_shared<GatedAudioSink>();
    std::shared_ptr<CountingVideoSink> videoSink = std::make_shared<CountingVideoSink>();

    Harness() = default;
    Harness(Harness&&) = default;
    Harness& operator=(Harness&&) = default;

    ~Harness() {
        // Stop the session first so its source/stream threads are released; otherwise the
        // still-open file streams pile up and later tests never get data.
        if (engine) {
            engine->post(Stop{});
            for (int i = 0; i < 500 && engine->snapshot().status != Status::Idle; ++i) {
                std::this_thread::sleep_for(1ms);
            }
        }
        engine.reset();
        if (scheduler) {
            scheduler->stop();
        }
    }

    static Harness create() {
        Harness h;
        static std::atomic<int> sequence{0};
        const auto name = "controlTest" + std::to_string(++sequence);
        h.scheduler = std::make_unique<ThreadScheduler>(name);
        h.notifier = std::make_unique<Notifier>(name);
        ComponentFactories factories;
        auto audioSink = h.audioSink;
        auto videoSink = h.videoSink;
        factories.makeAudioSink =
            [audioSink](std::shared_ptr<AudioInfo>) -> std::unique_ptr<AudioSink> {
            return std::make_unique<SharedAudioSink>(audioSink);
        };
        factories.makeVideoSink = [videoSink]() -> std::unique_ptr<VideoSink> {
            (void)videoSink;
            return std::make_unique<CountingVideoSink>();
        };
        factories.makeDecoder = [](bool isAudio) -> std::shared_ptr<IDecoder> {
            if (isAudio) {
                return std::make_shared<EchoAudioDecoder>();
            }
            return std::make_shared<EchoVideoDecoder>();
        };
        h.engine = Engine::create(
            *h.scheduler, *h.notifier,
            makePlaybackFactory(PlayerSetting{}, PlayerContext{}, factories));
        return h;
    }

    void dump(const char* tag) {
        const auto s = engine->snapshot();
        ADD_FAILURE() << tag << " status=" << static_cast<int>(s.status)
                      << " wantPlay=" << s.wantPlay << " pos=" << s.position.second()
                      << " buffered=" << s.buffered.second() << " duration=" << s.duration.second()
                      << " sinkBuffered=" << audioSink->buffered.load()
                      << " sinkConsumed=" << audioSink->consumed.load();
    }

    void prepare(const char* path, double endSec = 0.0) {
        Prepare prepare;
        prepare.item.path = path;
        if (endSec > 0) {
            prepare.playbackRange.startSec = 0.0;
            prepare.playbackRange.endSec = endSec;
        }
        // The default 2s start buffer cannot be satisfied by a bounded fake sink; the
        // production value is exercised on device.
        FlowConfig flow;
        flow.buffering.startBufferSec = 0.05;
        flow.buffering.rebufferEnterSec = 0.06;
        flow.buffering.rebufferExitSec = 0.072;
        prepare.setting.flowConfig = flow;
        engine->post(std::move(prepare));
    }

    // Drives the vsync hook while waiting; playMedia also consumes PCM so the clock advances.
    template <typename Predicate>
    bool pumpUntil(Predicate predicate, bool playMedia = false, int timeoutMs = 8000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            engine->notifyVsync();
            if (playMedia && (audioSink->buffered.load() > 4 || audioSink->ended.load())) {
                // After the decoder ends the hardware callback reads the rest out.
                audioSink->consume(1);
            }
            if (predicate()) {
                return true;
            }
            std::this_thread::sleep_for(1ms);
        }
        return predicate();
    }
};

}  // namespace

TEST(PlaybackControlTest, PlayThenPauseKeepsPauseInsteadOfFallingBackToBuffering) {
    auto h = Harness::create();
    h.prepare(kMp4Path);
    const bool ready = h.pumpUntil([&] {
        return h.engine->snapshot().status == Status::Ready;
    });
    if (!ready) {
        const auto s = h.engine->snapshot();
        ADD_FAILURE() << "status=" << static_cast<int>(s.status) << " buffered="
                      << s.buffered.second() << " duration=" << s.duration.second()
                      << " hasAudio=" << s.hasAudio << " written=" << h.audioSink->buffered.load();
    }
    ASSERT_TRUE(ready);
    EXPECT_FALSE(h.engine->snapshot().wantPlay);

    h.engine->post(Play{});
    ASSERT_TRUE(h.pumpUntil([&] {
        return h.engine->snapshot().status == Status::Ready && h.engine->snapshot().wantPlay;
    }));

    h.engine->post(Pause{});
    ASSERT_TRUE(h.pumpUntil([&] {
        return !h.engine->snapshot().wantPlay;
    }));
    // Starvation must not force Buffering while paused: keep pumping vsyncs with an empty
    // queue and the state has to stay Ready.
    for (int i = 0; i < 30; ++i) {
        h.engine->notifyVsync();
        std::this_thread::sleep_for(1ms);
    }
    EXPECT_EQ(h.engine->snapshot().status, Status::Ready);
    EXPECT_FALSE(h.engine->snapshot().wantPlay);

    h.engine->post(Play{});
    ASSERT_TRUE(h.pumpUntil([&] {
        return h.engine->snapshot().wantPlay && h.engine->snapshot().status == Status::Ready;
    }));
    h.engine->post(Stop{});
}

TEST(PlaybackControlTest, PausedVideoPipelineDoesNotBlockReadiness) {
    auto h = Harness::create();
    h.prepare(kMp4Path);
    ASSERT_TRUE(h.pumpUntil([&] {
        return h.engine->snapshot().status == Status::Ready;
    }));
    h.engine->post(Play{});
    ASSERT_TRUE(h.pumpUntil([&] {
        return h.engine->snapshot().wantPlay && h.engine->snapshot().status == Status::Ready;
    }));
    h.engine->post(Pause{});
    ASSERT_TRUE(h.pumpUntil([&] {
        return !h.engine->snapshot().wantPlay;
    }));
    // Drain the video queue with vsyncs; a paused pipeline may hold nothing without becoming
    // "buffering".
    for (int i = 0; i < 60; ++i) {
        h.engine->notifyVsync();
        std::this_thread::sleep_for(1ms);
        ASSERT_NE(h.engine->snapshot().status, Status::Buffering) << "vsync " << i;
    }
    h.engine->post(Stop{});
}

TEST(PlaybackControlTest, AccurateSeekWhilePausedLandsOnTargetAndStaysPaused) {
    auto h = Harness::create();
    h.prepare(kMp4Path);
    ASSERT_TRUE(h.pumpUntil([&] {
        return h.engine->snapshot().status == Status::Ready;
    }));
    h.engine->post(Seek{sec(2.0), SeekMode::Exact});
    const bool landed = h.pumpUntil([&] {
        const auto s = h.engine->snapshot();
        return s.status == Status::Ready && s.position.second() >= 1.999;
    }, false, 15000);
    if (!landed) {
        h.dump("seek-paused");
    }
    ASSERT_TRUE(landed);
    const auto position = h.engine->snapshot().position.second();
    EXPECT_GE(position, 1.999);
    EXPECT_LE(position, 2.2);
    EXPECT_FALSE(h.engine->snapshot().wantPlay);
    h.engine->post(Stop{});
}

TEST(PlaybackControlTest, SeekWhilePlayingResumesFromTheTarget) {
    auto h = Harness::create();
    h.prepare(kMp4Path);
    ASSERT_TRUE(h.pumpUntil([&] {
        return h.engine->snapshot().status == Status::Ready;
    }));
    h.engine->post(Play{});
    ASSERT_TRUE(h.pumpUntil([&] {
        return h.engine->snapshot().wantPlay && h.engine->snapshot().status == Status::Ready;
    }));
    h.engine->post(Seek{sec(2.0), SeekMode::Exact});
    const bool resumed = h.pumpUntil([&] {
        const auto s = h.engine->snapshot();
        return s.status == Status::Ready && s.wantPlay && s.position.second() >= 1.999;
    }, true);
    if (!resumed) {
        h.dump("seek-playing");
    }
    ASSERT_TRUE(resumed);
    EXPECT_LE(h.engine->snapshot().position.second(), 6.0);
    h.engine->post(Stop{});
}

TEST(PlaybackControlTest, LoopRestartsPlaybackWhenTheMediaEnds) {
    auto h = Harness::create();
    h.prepare(kMp4Path);
    ASSERT_TRUE(h.pumpUntil([&] {
        return h.engine->snapshot().status == Status::Ready;
    }));
    h.engine->post(SetRepeat{RepeatMode::One});
    h.engine->post(Play{});
    ASSERT_TRUE(h.pumpUntil([&] {
        const auto s = h.engine->snapshot();
        return s.wantPlay && s.status == Status::Ready;
    }, true));
    // Jump close to the end so the clip finishes inside the test timeout.
    h.engine->post(Seek{sec(63.0), SeekMode::Fast});

    double highest = 0.0;
    bool restarted = false;
    ASSERT_TRUE(h.pumpUntil([&] {
        const auto s = h.engine->snapshot();
        highest = std::max(highest, s.position.second());
        if (highest > 63.5 && s.position.second() < 62.0) {
            restarted = true;
        }
        return restarted;
    }, true, 20000));
    EXPECT_TRUE(restarted) << "highest=" << highest;
    h.engine->post(Stop{});
}

TEST(PlaybackControlTest, LoopDisabledEndsPlayback) {
    auto h = Harness::create();
    h.prepare(kMp4Path);
    ASSERT_TRUE(h.pumpUntil([&] {
        return h.engine->snapshot().status == Status::Ready;
    }));
    h.engine->post(Play{});
    ASSERT_TRUE(h.pumpUntil([&] {
        const auto s = h.engine->snapshot();
        return s.wantPlay && s.status == Status::Ready;
    }, true));
    h.engine->post(Seek{sec(63.0), SeekMode::Fast});
    const bool ended = h.pumpUntil([&] {
        return h.engine->snapshot().status == Status::Ended;
    }, true, 20000);
    if (!ended) {
        h.dump("loop-off");
    }
    ASSERT_TRUE(ended);
    h.engine->post(Stop{});
}

TEST(PlaybackControlTest, StoppingASessionLeavesNoSessionThreadAlive) {
    auto& threads = ThreadManager::shareInstance();
    // The log writer owns a process-wide thread that appears on the first log line.
    (void)LogOutput::shareInstance();
    auto h = Harness::create();
    const auto idle = threads.size();
    EXPECT_GE(idle, 2u);

    h.prepare(kMp4Path);
    ASSERT_TRUE(h.pumpUntil([&] {
        return h.engine->snapshot().status == Status::Ready;
    }));
    EXPECT_GT(threads.size(), idle);

    h.engine->post(Stop{});
    EXPECT_TRUE(h.pumpUntil([&] {
        return h.engine->snapshot().status == Status::Idle && threads.size() == idle;
    })) << "idle=" << idle << " now=" << threads.size();
}
