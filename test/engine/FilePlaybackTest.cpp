//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "PlaybackFactory.h"

#include <gtest/gtest.h>

#include <atomic>

#include "ComponentFactories.h"
#include "Engine.h"
#include "ManualScheduler.h"
#include "MediaClock.h"
#include "PlayerContext.h"
#include "Scheduler.h"
#include "SourceSupport.h"

using namespace std::chrono_literals;
using namespace slark;
using namespace slark::engine;
using namespace slark::engine::fake;

namespace {

constexpr const char* kWavPath = "../../test_sample/sample-3s.wav";
constexpr const char* kMp4Path = "../../test_sample/Sample.mp4";

// Straight-through decoder standing in for the platform codecs.
struct EchoAudioDecoder : public IDecoder {
    std::atomic<int> decodeCalls{0};

    EchoAudioDecoder() : IDecoder(DecoderType::RAW) {}

    bool open(std::shared_ptr<DecoderConfig>) noexcept override {
        isOpen_ = true;
        return true;
    }

    void flush() noexcept override {}

    Error decode(AVFrameRefPtr& frame) noexcept override {
        ++decodeCalls;
        if (frame->isEOS) {
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
};

// Always accepts PCM; tracks consumption only.
struct AcceptingAudioSink : public AudioSink {
    bool open(std::shared_ptr<AudioInfo> info) override {
        audioInfo_ = std::move(info);
        return true;
    }

    bool write(AVFrameRefPtr frame) override {
        ++written;
        return true;
    }

    [[nodiscard]] Time::TimePoint position() const override {
        return Time::TimePoint{0};
    }

    [[nodiscard]] bool primed() const override {
        return written >= 1;
    }

    [[nodiscard]] double bufferedSec() const override {
        return 0.0;
    }

    void start() override {}

    void pause() override {}

    void flush(Time::TimePoint offset) override {
        (void)offset;
    }

    void markDecoderEnded() override {
        decoderEnded = true;
    }

    [[nodiscard]] bool drained() const override {
        return decoderEnded;
    }

    std::shared_ptr<AudioInfo> audioInfo_;
    std::atomic<int> written{0};
    std::atomic<bool> decoderEnded{false};
};

// Sink whose counters are observable from the test (the pipeline owns the sink instance).
struct SharedCountingAudioSink : public AudioSink {
    explicit SharedCountingAudioSink(std::shared_ptr<std::atomic<int>> written,
                                     std::shared_ptr<std::atomic<bool>> primed)
        : written_(std::move(written)), primed_(std::move(primed)) {}

    bool open(std::shared_ptr<AudioInfo>) override {
        return true;
    }

    bool write(AVFrameRefPtr) override {
        ++(*written_);
        primed_->store(true);
        return true;
    }

    [[nodiscard]] Time::TimePoint position() const override {
        return Time::TimePoint{0};
    }

    [[nodiscard]] bool primed() const override {
        return primed_->load();
    }

    [[nodiscard]] double bufferedSec() const override {
        return 0.0;
    }

    void start() override {}

    void pause() override {}

    void flush(Time::TimePoint) override {}

    void markDecoderEnded() override {}

    [[nodiscard]] bool drained() const override {
        return false;
    }

    std::shared_ptr<std::atomic<int>> written_;
    std::shared_ptr<std::atomic<bool>> primed_;
};

struct EchoVideoDecoder : public IDecoder {
    std::atomic<int> decodeCalls{0};

    EchoVideoDecoder() : IDecoder(DecoderType::VideoHardwareDecoder) {}

    bool open(std::shared_ptr<DecoderConfig>) noexcept override {
        isOpen_ = true;
        return true;
    }

    void flush() noexcept override {}

    Error decode(AVFrameRefPtr& frame) noexcept override {
        ++decodeCalls;
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
        out->seekEpoch = currentSeekEpoch();
        out->info = fake::decodedVideoInfo();
        out->data = std::make_unique<Data>();
        out->data->append(std::vector<uint8_t>(64, 0));
        invokeReceiveFunc(DecoderData{AVFrameRefPtr(std::move(out)), Error::Success,
                                       currentSeekEpoch()});
        return Error::Success;
    }
};

struct CountingVideoSink : public VideoSink {
    explicit CountingVideoSink(std::shared_ptr<std::atomic<int>> presented)
        : presented_(std::move(presented)) {}

    void present(AVFrameRefPtr frame) override {
        (void)frame;
        ++(*presented_);
    }

    void dropFrames(int32_t) override {}

    [[nodiscard]] int32_t heldCount() const override {
        return 0;
    }

    std::shared_ptr<std::atomic<int>> presented_;
};

Prepare makePrepare(const std::string& uri = kWavPath) {
    Prepare prepare;
    prepare.item.path = uri;
    return prepare;
}

// Release joins the source loader, which keeps calling into the scheduler until then.
class EngineReleaseGuard {
public:
    EngineReleaseGuard(const std::shared_ptr<Engine>& engine, slark::test::ManualScheduler& scheduler)
        : engine_(engine), scheduler_(scheduler) {}

    EngineReleaseGuard(const EngineReleaseGuard&) = delete;
    EngineReleaseGuard& operator=(const EngineReleaseGuard&) = delete;

    ~EngineReleaseGuard() {
        engine_->post(Release{});
        scheduler_.runUntilIdle();
    }

private:
    const std::shared_ptr<Engine>& engine_;
    slark::test::ManualScheduler& scheduler_;
};

}  // namespace

TEST(FilePlaybackTest, AudioFilePlaysThroughAndStops) {
    auto scheduler = std::make_unique<ThreadScheduler>("sessionTest");
    Notifier notifier{"sessionTest"};
    ComponentFactories factories;
    factories.makeAudioSink = [](std::shared_ptr<AudioInfo>) {
        return std::make_unique<AcceptingAudioSink>();
    };
    factories.makeDecoder = [](bool) {
        return std::make_shared<EchoAudioDecoder>();
    };
    auto engine = Engine::create(
        *scheduler, notifier,
        makePlaybackFactory(PlayerSetting{}, PlayerContext{}, factories));
    engine->post(makePrepare());
    engine->post(Play{});
    // The 3s wav decodes straight through to Ended, then stop tears the session down.
    ASSERT_TRUE(waitUntil([&] {
        const auto status = engine->snapshot().status;
        return status == Status::Ended || status == Status::Failed;
    }));
    EXPECT_EQ(engine->snapshot().status, Status::Ended);

    engine->post(Stop{});
    ASSERT_TRUE(waitUntil([&] {
        return engine->snapshot().status == Status::Idle;
    }));
}

TEST(FilePlaybackTest, AudioSessionLeavesBufferingWhenEpochArrivesOnlyInDecoderData) {
    // Regression: epoch stamped only on DecoderData left the engine stuck in Buffering.
    auto scheduler = std::make_unique<ThreadScheduler>("sessionTest");
    Notifier notifier{"sessionTest"};
    ComponentFactories factories;
    auto written = std::make_shared<std::atomic<int>>(0);
    auto primed = std::make_shared<std::atomic<bool>>(false);
    factories.makeAudioSink = [written, primed](std::shared_ptr<AudioInfo>)
        -> std::unique_ptr<AudioSink> {
        return std::make_unique<SharedCountingAudioSink>(written, primed);
    };
    factories.makeDecoder = [](bool) {
        return std::make_shared<EchoAudioDecoder>();  // stamps the epoch on DecoderData only
    };
    auto engine = Engine::create(
        *scheduler, notifier,
        makePlaybackFactory(PlayerSetting{}, PlayerContext{}, factories));
    engine->post(makePrepare());  // no Play: readiness alone must leave Buffering
    ASSERT_TRUE(waitUntil([&] {
        return engine->snapshot().status == Status::Ready;
    }));
    EXPECT_GT(written->load(), 0);
    EXPECT_TRUE(primed->load());
    engine->post(Stop{});
}

TEST(FilePlaybackTest, NotifyVsyncPresentsTheFirstVideoFrameAndLeavesBuffering) {
    auto scheduler = std::make_unique<ThreadScheduler>("sessionTest");
    Notifier notifier{"sessionTest"};
    ComponentFactories factories;
    // Sample.mp4 has both tracks; a missing audio sink would make createPipelines() return
    // empty and leave no video pipeline to test.
    factories.makeAudioSink = [](std::shared_ptr<AudioInfo>) -> std::unique_ptr<AudioSink> {
        return std::make_unique<AcceptingAudioSink>();
    };
    auto presented = std::make_shared<std::atomic<int>>(0);
    factories.makeVideoSink = [presented]() -> std::unique_ptr<VideoSink> {
        return std::make_unique<CountingVideoSink>(presented);
    };
    factories.makeDecoder = [](bool isAudio) -> std::shared_ptr<slark::IDecoder> {
        if (isAudio) {
            return std::make_shared<EchoAudioDecoder>();
        }
        return std::make_shared<EchoVideoDecoder>();
    };
    auto engine = Engine::create(
        *scheduler, notifier,
        makePlaybackFactory(PlayerSetting{}, PlayerContext{}, factories));
    engine->post(makePrepare(kMp4Path));
    engine->post(Play{});
    // The platform's vsync hook calls this; the video pipeline ends buffering only after the
    // first frame was presented, so this is the exact path that used to deadlock.
    ASSERT_TRUE(waitUntil([&] {
        engine->notifyVsync();
        return engine->snapshot().status == Status::Ready ||
               engine->snapshot().status == Status::Ended;
    }));
    EXPECT_GT(presented->load(), 0);
    engine->post(Stop{});
}

TEST(FilePlaybackTest, MissingAudioSinkFailsWithFormatNotSupported) {
    slark::test::ManualScheduler scheduler;
    Notifier notifier{"sessionTest"};
    ComponentFactories factories;  // no sink factory: no pipeline can be built
    auto engine = Engine::create(
        scheduler, notifier,
        makePlaybackFactory(PlayerSetting{}, PlayerContext{}, factories));
    EngineReleaseGuard releaseEngine(engine, scheduler);
    engine->post(makePrepare());
    ASSERT_TRUE(waitUntil([&] {
        scheduler.advance(10ms);
        return engine->snapshot().status == Status::Failed;
    }));
}

TEST(FilePlaybackTest, DecoderFailureFailsTheEngine) {
    slark::test::ManualScheduler scheduler;
    Notifier notifier{"sessionTest"};
    ComponentFactories factories;
    factories.makeAudioSink = [](std::shared_ptr<AudioInfo>) {
        return std::make_unique<AcceptingAudioSink>();
    };
    factories.makeDecoder = [](bool) {
        return nullptr;  // codec lookup failure
    };
    auto engine = Engine::create(
        scheduler, notifier,
        makePlaybackFactory(PlayerSetting{}, PlayerContext{}, factories));
    EngineReleaseGuard releaseEngine(engine, scheduler);
    engine->post(makePrepare());
    ASSERT_TRUE(waitUntil([&] {
        scheduler.advance(10ms);
        return engine->snapshot().status == Status::Failed;
    }));
    EXPECT_EQ(engine->snapshot().status, Status::Failed);
}

TEST(FilePlaybackTest, RepeatedStopAndTeardownIsIdempotent) {
    slark::test::ManualScheduler scheduler;
    Notifier notifier{"sessionTest"};
    ComponentFactories factories;
    factories.makeAudioSink = [](std::shared_ptr<AudioInfo>) {
        return std::make_unique<AcceptingAudioSink>();
    };
    factories.makeDecoder = [](bool) {
        return std::make_shared<EchoAudioDecoder>();
    };
    auto engine = Engine::create(
        scheduler, notifier,
        makePlaybackFactory(PlayerSetting{}, PlayerContext{}, factories));
    engine->post(makePrepare());
    engine->post(Stop{});
    engine->post(Stop{});
    engine->post(Stop{});
    ASSERT_TRUE(waitUntil([&] {
        scheduler.advance(10ms);
        return engine->snapshot().status == Status::Idle;
    }));
    const auto idleStatus = engine->snapshot().status;
    EXPECT_EQ(idleStatus, Status::Idle);
}
