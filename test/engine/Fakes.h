//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "EngineMessages.h"
#include "Engine.h"
#include "ManualScheduler.h"

namespace slark::engine::fake {

using Wake = PlaybackFactory::Wake;

struct FakeSource final : Source {
    struct SeekCall {
        Time::TimePoint target;
        SeekEpoch epoch;
    };

    MediaInfo facts;
    Time::TimePoint cache{};
    bool loading = true;
    std::optional<Error> fail;
    std::vector<SeekCall> seeks;
    std::vector<Time::TimePoint> retargets;
    bool coversTarget = false;
    std::vector<Time::TimePoint> regulated;
    std::vector<bool> seekingCalls;
    int* closeCounter = nullptr;
    Wake wake;
    // Lets a test observe ordering against pipeline flushes.
    std::function<void()> onSeek;
    ErrorDetail failDetail;

    [[nodiscard]] MediaInfo info() const override {
        return facts;
    }

    [[nodiscard]] Time::TimePoint cacheEnd() const override {
        return cache;
    }

    [[nodiscard]] bool isLoading() const override {
        return loading;
    }

    [[nodiscard]] std::optional<Error> failure() const override {
        return fail;
    }

    [[nodiscard]] ErrorDetail failureDetail() const override {
        return failDetail;
    }

    [[nodiscard]] Receiver<AVFrameRefPtr>* frames(TrackType) override {
        return nullptr;
    }

    void seekTo(Time::TimePoint target, SeekEpoch epoch) override {
        if (onSeek) {
            onSeek();
        }
        seeks.push_back({target, epoch});
    }

    bool retarget(Time::TimePoint target) override {
        if (coversTarget) {
            retargets.push_back(target);
        }
        return coversTarget;
    }

    void regulate(Time::TimePoint clockNow) override {
        regulated.push_back(clockNow);
    }

    void setSeeking(bool seeking) override {
        seekingCalls.push_back(seeking);
    }

    void close() override {
        if (closeCounter) {
            ++*closeCounter;
        }
    }

    void setTracks(double durationSec, bool seekable = true, bool live = false) {
        facts.tracksKnown = true;
        facts.isSeekable = seekable;
        facts.isLive = live;
        facts.duration = Time::TimePoint::fromSeconds(durationSec);
        wake();
    }

    void setCache(double sec) {
        cache = Time::TimePoint::fromSeconds(sec);
        wake();
    }
};

struct FakePipeline final : Pipeline {
    struct FlushCall {
        SeekEpoch epoch;
        Time::TimePoint target;
        SeekMode mode;
    };

    bool blocking = true;
    bool isEnded = false;
    std::optional<Error> fail;
    std::optional<Time::TimePoint> wakeup;
    std::vector<FlushCall> flushes;
    std::vector<FlushCall> retargets;
    std::vector<float> volumes;
    bool presented = false;
    MediaClock* clock = nullptr;
    Wake wake;
    std::optional<SeekEpoch> readyForEpoch;
    SeekEpoch currentEpoch = kInvalidSeekEpoch;
    // Lets a test observe ordering against the source seek.
    std::function<void()> onFlush;

    void flush(SeekEpoch epoch, Time::TimePoint target, SeekMode mode) override {
        if (onFlush) {
            onFlush();
        }
        flushes.push_back({epoch, target, mode});
        currentEpoch = epoch;
        isEnded = false;
    }

    void retarget(SeekEpoch epoch, Time::TimePoint target, SeekMode mode) override {
        retargets.push_back({epoch, target, mode});
    }

    void setClock(MediaClock& c) override {
        clock = &c;
    }

    void setVolume(float volume) override {
        volumes.push_back(volume);
    }

    [[nodiscard]] bool presentedFrame() const override {
        return presented;
    }

    [[nodiscard]] bool ready(SeekEpoch epoch, Time::TimePoint) const override {
        return readyForEpoch == epoch;
    }

    [[nodiscard]] bool ended() const override {
        return isEnded;
    }

    [[nodiscard]] std::optional<Error> failure() const override {
        return fail;
    }

    [[nodiscard]] bool blocksReady() const override {
        return blocking;
    }

    [[nodiscard]] std::optional<Time::TimePoint> nextWakeup() const override {
        return wakeup;
    }

    void becomeReady() {
        becomeReady(currentEpoch);
    }

    void becomeReady(SeekEpoch epoch) {
        readyForEpoch = epoch;
        wake();
    }

    void end() {
        isEnded = true;
        wake();
    }

    void present() {
        presented = true;
        wake();
    }
};

// Hands out raw pointers so tests can drive the fakes the engine owns.
class FakeFactory {
 public:
    FakeSource* source = nullptr;
    std::vector<FakePipeline*> pipelines;
    std::vector<bool> pipelineBlocking{true};
    bool nullSource = false;
    bool emptyPipelines = false;
    int sourcesCreated = 0;
    int sourcesClosed = 0;

    std::unique_ptr<Source> createSource(const Prepare&, SeekEpoch, Wake wake) {
        if (nullSource) {
            return nullptr;
        }
        auto s = std::make_unique<FakeSource>();
        s->wake = std::move(wake);
        s->closeCounter = &sourcesClosed;
        source = s.get();
        ++sourcesCreated;
        return s;
    }

    std::vector<std::unique_ptr<Pipeline>> createPipelines(Source&,
                                                           const MediaInfo&,
                                                           SeekEpoch epoch,
                                                           Wake wake) {
        std::vector<std::unique_ptr<Pipeline>> out;
        pipelines.clear();
        if (emptyPipelines) {
            return out;
        }
        for (const bool blocking : pipelineBlocking) {
            auto pipeline = std::make_unique<FakePipeline>();
            pipeline->blocking = blocking;
            pipeline->currentEpoch = epoch;
            pipeline->wake = wake;
            pipelines.push_back(pipeline.get());
            out.push_back(std::move(pipeline));
        }
        return out;
    }

    // The engine holds copies of these hooks; the fake must outlive it.
    PlaybackFactory hooks() {
        PlaybackFactory factory;
        factory.createSource = [this](const Prepare& prepare, SeekEpoch epoch, Wake wake) {
            return createSource(prepare, epoch, std::move(wake));
        };
        factory.createPipelines = [this](Source& source, const MediaInfo& info, SeekEpoch epoch,
                                         Wake wake) {
            return createPipelines(source, info, epoch, std::move(wake));
        };
        return factory;
    }
};

struct Recorder : IPlayerObserver {
    std::vector<PlayerState> states;
    std::vector<double> times;
    std::vector<std::pair<PlayerEvent, std::string>> events;
    std::function<void()> onState;

    void notifyPlayedTime(std::string_view, double time) override {
        times.push_back(time);
    }

    void notifyPlayerState(std::string_view, PlayerState state) override {
        states.push_back(state);
        if (onState) {
            onState();
        }
    }

    void notifyPlayerEvent(std::string_view, PlayerEvent event, std::string value) override {
        events.emplace_back(event, std::move(value));
    }

    [[nodiscard]] int count(PlayerEvent event) const {
        return static_cast<int>(
            std::ranges::count(events, event, &decltype(events)::value_type::first));
    }
};

class EngineTest : public ::testing::Test {
 protected:
    void SetUp() override {
        notifier.addObserver(rec);
        engine = Engine::create(sched, notifier, fakeFactory.hooks());
    }

    void send(PlayerAction command) {
        engine->post(std::move(command));
        sched.runUntilIdle();
    }

    void prepare(Prepare prepare = {}) {
        prepare.item.path = "fake";
        send(std::move(prepare));
    }

    void open(double durationSec = 10.0, bool seekable = true) {
        prepare();
        src().setTracks(durationSec, seekable);
        sched.runUntilIdle();
    }

    // Opened, pipeline ready and enough cache to start: status Ready.
    void openReady(double durationSec = 10.0) {
        open(durationSec);
        src().setCache(5.0);
        pipeline().becomeReady();
        sched.runUntilIdle();
    }

    FakeSource& src() {
        return *factory->source;
    }

    FakePipeline& pipeline(size_t i = 0) {
        return *factory->pipelines.at(i);
    }

    [[nodiscard]] Status status() const {
        return engine->snapshot().status;
    }

    slark::test::ManualScheduler sched;
    Notifier notifier{"p"};
    std::shared_ptr<Recorder> rec = std::make_shared<Recorder>();
    // Declared before the engine so it outlives the hooks the engine holds.
    FakeFactory fakeFactory;
    FakeFactory* factory = &fakeFactory;
    std::shared_ptr<Engine> engine;
};

}  // namespace slark::engine::fake
