//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "PipelineConfig.h"
#include "PipelineError.h"
#include "PipelineEvent.h"
#include "Time.hpp"

#include <chrono>
#include <functional>
#include <optional>

namespace slark {

using StreamControlFunc = std::function<void()>;
using DemuxedDurationFunc = std::function<double()>;
using PrebufferCompleteFunc = std::function<void()>;
using DropOldestFunc = std::function<void()>;

/**
 * @brief 背压与缓冲监控器。
 *
 * 线程安全约定：所有方法必须在 ownerThread 调用。
 * 通过 pauseStream/resumeStream 回调控制 IStream 数据流速。
 */
class BufferMonitor {
public:
    BufferMonitor(const BackpressureConfig& config,
                  PipelineEventCallback onEvent,
                  DemuxedDurationFunc demuxedDuration,
                  StreamControlFunc pauseStream,
                  StreamControlFunc resumeStream,
                  PrebufferCompleteFunc onPrebufferComplete = nullptr,
                  DropOldestFunc dropOldest = nullptr) noexcept;

    void onDataEnqueued() noexcept;

    void onHeartbeat() noexcept;

    // DemuxHeaderParsed 后调用，进入预缓冲状态
    void startPrebuffering() noexcept;

    // seek 后重置状态
    void reset() noexcept;

    void onStarvation(bool isLocalSource) noexcept;

    void resetStarvationCount() noexcept {
        starvationCount_ = 0;
    }

    [[nodiscard]] bool isPrebuffering() const noexcept {
        return isPrebuffering_;
    }

    [[nodiscard]] bool isRebuffering() const noexcept {
        return isRebuffering_;
    }

private:
    void regulateDataFlow() noexcept;
    void checkPrebufferComplete() noexcept;
    void checkRebufferComplete() noexcept;

    [[nodiscard]] double demuxedDuration() const noexcept {
        return demuxedDuration_ ? demuxedDuration_() : 0.0;
    }

    void pauseStream() noexcept {
        if (!streamPaused_ && pauseStream_) {
            pauseStream_();
            streamPaused_ = true;
            pauseTime_ = Clock::now();
            pauseFailCount_ = 0;
        }
    }

    void resumeStream() noexcept {
        if (streamPaused_ && resumeStream_) {
            resumeStream_();
            streamPaused_ = false;
            resumeTime_ = Clock::now();
            pauseTime_.reset();
        }
    }

    void fireEvent(PipelineEvent event,
                   std::optional<PipelineError> error = std::nullopt) noexcept {
        if (onEvent_) {
            onEvent_(event, std::move(error));
        }
    }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::steady_clock::time_point;

    static constexpr int32_t kStarvationThreshold = 5;
    static constexpr int32_t kPauseFailThreshold = 3;

    BackpressureConfig config_;
    PipelineEventCallback onEvent_;
    DemuxedDurationFunc demuxedDuration_;
    StreamControlFunc pauseStream_;
    StreamControlFunc resumeStream_;
    PrebufferCompleteFunc onPrebufferComplete_;
    DropOldestFunc dropOldest_;

    bool streamPaused_ = false;
    bool isPrebuffering_ = false;
    bool isRebuffering_ = false;
    bool readyFired_ = false;

    int32_t pauseFailCount_ = 0;
    int32_t starvationCount_ = 0;

    std::optional<TimePoint> pauseTime_;
    std::optional<TimePoint> resumeTime_;
    std::optional<TimePoint> lastCheckTime_;
};

} // namespace slark
namespace slark
