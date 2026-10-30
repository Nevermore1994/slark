//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "AVFrame.hpp"
#include "Channel.hpp"
#include "Error.h"
#include "IDecoder.h"
#include "Pipeline.h"
#include "SeekEpoch.h"
#include "SeekMode.h"
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

    [[nodiscard]] std::string debugState() const override;
    [[nodiscard]] bool ended() const override;
    [[nodiscard]] std::optional<Error> failure() const override;

 private:
    void run();
    void handleInput(AVFrameRefPtr& frame);
    bool decodeOne(AVFrameRefPtr& frame);
    void handleDecoded(slark::DecoderData data);
    void drainDecoderOutput();
    void pump();
    void fail(slark::Error error);
    void wake();

    Receiver<AVFrameRefPtr>& input_;
    std::function<void()> wake_;
    DecoderFactory decoderFactory_;
    std::shared_ptr<slark::IDecoder> decoder_;
    std::unique_ptr<AudioSink> sink_;
    MediaClock* clock_ = nullptr;

    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool closed_ = false;

    // Guarded by mutex_ and owned by the pipeline thread between commands.
    SeekEpoch epoch_ = kInvalidSeekEpoch;
    Time::TimePoint target_{};
    SeekMode seekMode_ = SeekMode::Exact;
    bool playing_ = false;
    bool eosReceived_ = false;
    bool decoderEnded_ = false;
    std::optional<double> lastDecodedPts_;
    // Stays ready once the prefill was reached: dipping below it is normal while playing.
    mutable bool primedOnce_ = false;
    std::deque<AVFrameRefPtr> pendingOutput_;
    Time::TimePoint lastAnchored_{};
    std::optional<Error> failure_;

    static constexpr size_t kMaxPendingOutput = 32;
};

}  // namespace slark::engine
