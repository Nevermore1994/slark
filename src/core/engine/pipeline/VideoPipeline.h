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
#include "SeekEpoch.h"
#include "SeekMode.h"
#include "Time.hpp"

namespace slark::engine {

class VideoSink;
class MediaClock;

class VideoPipeline final : public Pipeline {
 public:
    using DecoderFactory = std::function<std::shared_ptr<slark::IDecoder>()>;
    // True when the cache runs far ahead of the clock and pending input should skip to a keyframe.
    using CatchUpProbe = std::function<bool()>;

    struct Config {
        int32_t maxQueueFrames = 8;
        int32_t starvationThreshold = 5;
        int32_t maxDropPerVsync = 8;
    };

    // The source owns the channel; Engine tears pipelines down before the source.
    VideoPipeline(Receiver<AVFrameRefPtr>& input,
              DecoderFactory decoderFactory,
              std::shared_ptr<VideoSink> sink,
              Config config,
              SeekEpoch epoch,
              Time::TimePoint target,
              std::function<void()> wake);
    ~VideoPipeline() override;

    void flush(SeekEpoch epoch, Time::TimePoint target, SeekMode mode) override;
    void retarget(SeekEpoch epoch, Time::TimePoint target, SeekMode mode) override;
    void setClock(MediaClock& clock) override;
    void setCatchUpProbe(CatchUpProbe probe);

    [[nodiscard]] std::uint64_t catchUpCount() const;

    [[nodiscard]] std::string debugState() const override;
    [[nodiscard]] DebugInfo debugInfo() const override;

    [[nodiscard]] bool ready(SeekEpoch epoch, Time::TimePoint target) const override;
    [[nodiscard]] bool ended() const override;
    [[nodiscard]] std::optional<Error> failure() const override;

    // Called from the vsync/render thread.
    void onVsync(Time::TimePoint clockNow) override;

    void setPlaying(bool playing) override;
    [[nodiscard]] bool presentedFrame() const override;

 private:
    void run();
    void pullInput();
    void drainInbox();
    bool attempt(AVFrameRefPtr& frame);
    void handleDecoded(slark::DecoderData data);
    void drainDecoderOutput();
    void catchUpToKeyframeLocked();
    void waitForWork();
    void waitForWake(std::optional<std::chrono::milliseconds> timeout);
    [[nodiscard]] std::optional<std::chrono::milliseconds> waitTimeout() const;
    [[nodiscard]] bool stopping() const;
    [[nodiscard]] bool queueFull() const;
    [[nodiscard]] bool refusalExceededLimit();
    void noteProgress();
    void fail(slark::Error error);
    void wakeLoop();
    void advanceLocked(Time::TimePoint clockNow);
    [[nodiscard]] bool readyLocked() const noexcept;
    [[nodiscard]] bool endedLocked() const noexcept;
    [[nodiscard]] int32_t queueCapacityLocked() const;

    Receiver<AVFrameRefPtr>& input_;
    std::function<void()> wake_;
    DecoderFactory decoderFactory_;
    std::shared_ptr<slark::IDecoder> decoder_;
    std::shared_ptr<VideoSink> sink_;
    CatchUpProbe catchUpProbe_;
    std::uint64_t catchUpCount_ = 0;
    MediaClock* clock_ = nullptr;
    const Config config_;

    std::shared_ptr<LoopThread> thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool closed_ = false;
    bool wakeRequested_ = false;
    // Guarded by mutex_: set when the decoder refuses input, cleared by any decoder progress.
    std::optional<std::chrono::steady_clock::time_point> refusedSince_;
    // The decoder's wakeup callback holds this gate; the destructor closes it before teardown.
    std::shared_ptr<AccessGate> wakeGate_ = std::make_shared<AccessGate>();

    // Guarded by mutex_; mutated on the pipeline thread between commands, read on vsync.
    SeekEpoch epoch_ = kInvalidSeekEpoch;
    Time::TimePoint target_{};
    SeekMode seekMode_ = SeekMode::Exact;
    bool eosReceived_ = false;
    bool decoderEnded_ = false;
    bool prerolled_ = false;
    bool presentedFrame_ = false;
    bool starved_ = false;
    int32_t starvedVsyncs_ = 0;
    bool playing_ = false;
    std::deque<AVFrameRefPtr> queue_;
    std::optional<Error> failure_;

    // Pipeline thread only.
    std::deque<AVFrameRefPtr> inbox_;
    AVFrameRefPtr staged_;
    bool inputClosed_ = false;
    bool blockedByCodec_ = false;
    bool outputPending_ = false;
    // Guarded by mutex_: the pipeline stopped on a full presentation queue and waits for vsync.
    bool blockedOnFull_ = false;

    struct ThreadSnapshot {
        size_t inboxDepth = 0;
        bool staged = false;
        bool inputClosed = false;
        bool blockedByCodec = false;
        bool outputPending = false;
    };

    // Written by the pipeline thread only, read under mutex_ by debugInfo().
    ThreadSnapshot threadSnapshot_;
};

}  // namespace slark::engine
