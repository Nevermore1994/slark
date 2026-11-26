//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

#include "AVFrame.hpp"
#include "AccessGate.hpp"
#include "Channel.hpp"
#include "Error.h"
#include "IDecoder.h"
#include "LoopThread.h"
#include "Pipeline.h"
#include "EngineTypes.h"
#include "Time.hpp"

namespace slark::engine {

class AudioSink;
class MediaClock;

class AudioPipeline final : public Pipeline {
 public:
    // Returns an opened decoder ready to decode; null fails the pipeline.
    using DecoderFactory = std::function<std::shared_ptr<slark::IDecoder>()>;

    // Engine drops pipelines before the source that owns the channel; wake fires on state changes.
    AudioPipeline(Receiver<AVFrameRefPtr>& input,
              DecoderFactory decoderFactory,
              std::unique_ptr<AudioSink> sink,
              SeekEpoch epoch,
              Time::TimePoint target,
              std::function<void()> wake);
    ~AudioPipeline() override;

    void flush(SeekEpoch epoch, Time::TimePoint target, SeekMode mode) override;
    void retarget(SeekEpoch epoch, Time::TimePoint target, SeekMode mode) override;
    void setClock(MediaClock& clock) override;
    // Starts or pauses actual playback; prefill keeps running while paused.
    void setPlaying(bool playing) override;
    void setVolume(float volume) override;

    [[nodiscard]] bool ready(SeekEpoch epoch, Time::TimePoint target) const override;
    [[nodiscard]] bool starved() const override;

    [[nodiscard]] std::string debugState() const override;
    [[nodiscard]] DebugInfo debugInfo() const override;
    [[nodiscard]] bool ended() const override;
    [[nodiscard]] std::optional<Error> failure() const override;
    [[nodiscard]] ErrorDetail failureDetail() const override;
    void abort() override;

 private:
    void run();
    void pullInput();
    void drainInbox();
    bool attempt(AVFrameRefPtr& frame);
    void handleDecoded(slark::DecoderData data);
    void drainDecoderOutput();
    void pump();
    bool updateUnderrunLocked();
    void rebaseLocked(Time::TimePoint target);
    void waitForWork();
    void waitForWake(std::optional<std::chrono::milliseconds> timeout);
    [[nodiscard]] std::optional<std::chrono::milliseconds> waitTimeout() const;
    [[nodiscard]] std::chrono::milliseconds sinkTickLocked() const;
    [[nodiscard]] bool stopping() const;
    [[nodiscard]] bool outputFull() const;
    [[nodiscard]] bool refusalExceededLimit();
    void noteProgress();
    void fail(slark::Error error, int32_t origin = 0);
    void wakeLoop();

    Receiver<AVFrameRefPtr>& input_;
    std::function<void()> wake_;
    DecoderFactory decoderFactory_;
    std::shared_ptr<slark::IDecoder> decoder_;
    std::unique_ptr<AudioSink> sink_;
    MediaClock* clock_ = nullptr;

    std::shared_ptr<LoopThread> thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool closed_ = false;
    bool wakeRequested_ = false;
    // Guarded by mutex_: set when the decoder refuses input, cleared by any decoder progress.
    std::optional<std::chrono::steady_clock::time_point> refusedSince_;
    // The decoder's wakeup callback holds this gate; the destructor closes it before teardown.
    std::shared_ptr<AccessGate> wakeGate_ = std::make_shared<AccessGate>();

    // Guarded by mutex_ and owned by the pipeline thread between commands.
    SeekEpoch epoch_ = kInvalidSeekEpoch;
    Time::TimePoint target_{};
    bool playing_ = false;
    bool eosReceived_ = false;
    bool decoderEnded_ = false;
    std::optional<double> lastDecodedPts_;
    // Stays ready once the prefill was reached: dipping below it is normal while playing.
    mutable bool primedOnce_ = false;
    // An empty hardware buffer after prefill means audio stopped advancing while the clock runs.
    bool starved_ = false;
    int underrunTicks_ = 0;
    std::deque<AVFrameRefPtr> pendingOutput_;
    // Frames the sink took since flush(); bounded by its capacity, dropped when playback starts.
    std::deque<AVFrameRefPtr> written_;
    bool recording_ = true;
    Time::TimePoint lastAnchored_{};
    std::optional<Error> failure_;
    int32_t failureOrigin_ = 0;

    // Pipeline thread only.
    std::deque<AVFrameRefPtr> inbox_;
    AVFrameRefPtr staged_;
    bool inputClosed_ = false;
    bool blockedByCodec_ = false;

    static constexpr size_t kMaxPendingOutput = 32;
    // Underruns need several empty sink ticks before the player rebuffers; a single
    // late frame must not flap Buffering on and off.
    static constexpr int kUnderrunTicks = 5;

    struct ThreadSnapshot {
        size_t inboxDepth = 0;
        bool staged = false;
        bool inputClosed = false;
        bool blockedByCodec = false;
    };

    // Written by the pipeline thread only, read under mutex_ by debugInfo().
    ThreadSnapshot threadSnapshot_;
};

}  // namespace slark::engine
