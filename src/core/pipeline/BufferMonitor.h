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

/**
 * @brief BufferMonitor 的委托接口，由 PlayPipeline 实现。
 *
 * 所有方法均在 ownerThread 调用。
 */
struct IBufferMonitorDelegate {
    virtual ~IBufferMonitorDelegate() = default;

    [[nodiscard]] virtual double demuxedDuration() const noexcept = 0;
    virtual void pauseStream() noexcept = 0;
    virtual void resumeStream() noexcept = 0;
    [[nodiscard]] virtual int32_t audioDecodedFrameCount() const noexcept = 0;
    [[nodiscard]] virtual int32_t videoDecodedFrameCount() const noexcept = 0;
    virtual void pauseDecoder() noexcept = 0;
    virtual void resumeDecoder() noexcept = 0;
    virtual void onPrebufferComplete() noexcept = 0;
    virtual void onDropOldest() noexcept = 0;
    virtual void onBufferEvent(PipelineEvent event,
                               std::optional<PipelineError> error) noexcept = 0;
};

/**
 * @brief 背压与缓冲监控器。
 *
 * 线程安全约定：所有方法必须在 ownerThread 调用。
 * 通过 IBufferMonitorDelegate 控制 IStream 数据流速并上报事件。
 */
class BufferMonitor {
public:
    BufferMonitor(const BackpressureConfig& config, IBufferMonitorDelegate& delegate) noexcept;

    // demux 侧：有新的已解封装数据入队时调用
    void onDemuxedDataEnqueued() noexcept;

    // decoder 侧：有新的已解码帧入队时调用
    void onDecodedDataEnqueued() noexcept;

    void onHeartbeat() noexcept;

    // DemuxHeaderParsed 后调用，进入预缓冲状态
    void startPrebuffering() noexcept;

    // seek 后重置状态
    void reset() noexcept;

    // 快速推帧期间暂停背压（demux 侧不受水位线限制）
    void pauseBackpressure() noexcept {
        backpressurePaused_ = true;
        resumeStream();
    }

    // 快速推帧结束后恢复背压
    void resumeBackpressure() noexcept {
        backpressurePaused_ = false;
    }

    // 文件读取到结尾时通知，停止 IO/demux 背压
    void setDemuxEof() noexcept {
        demuxEof_ = true;
    }

    // 解码器输入耗尽时通知，停止 decoder 背压
    void setDecoderEof() noexcept {
        decoderEof_ = true;
    }

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
    void regulateDecoderFlow() noexcept;
    void checkPrebufferComplete() noexcept;
    void checkRebufferComplete() noexcept;

    [[nodiscard]] double demuxedDuration() const noexcept {
        return delegate_.demuxedDuration();
    }

    void pauseStream() noexcept {
        if (!streamPaused_) {
            delegate_.pauseStream();
            streamPaused_ = true;
            pauseFailCount_ = 0;
        }
    }

    void resumeStream() noexcept {
        if (streamPaused_) {
            delegate_.resumeStream();
            streamPaused_ = false;
        }
    }

    void pauseAudioDecoder() noexcept {
        if (!audioDecoderPaused_) {
            delegate_.pauseDecoder();
            audioDecoderPaused_ = true;
        }
    }

    void resumeAudioDecoder() noexcept {
        if (audioDecoderPaused_) {
            delegate_.resumeDecoder();
            audioDecoderPaused_ = false;
        }
    }

    void pauseVideoDecoder() noexcept {
        if (!videoDecoderPaused_) {
            delegate_.pauseDecoder();
            videoDecoderPaused_ = true;
        }
    }

    void resumeVideoDecoder() noexcept {
        if (videoDecoderPaused_) {
            delegate_.resumeDecoder();
            videoDecoderPaused_ = false;
        }
    }

    void fireEvent(PipelineEvent event,
                   std::optional<PipelineError> error = std::nullopt) noexcept {
        delegate_.onBufferEvent(event, std::move(error));
    }

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = std::chrono::steady_clock::time_point;

    static constexpr int32_t kStarvationThreshold = 5;
    static constexpr int32_t kPauseFailThreshold = 3;

    BackpressureConfig config_;
    IBufferMonitorDelegate& delegate_;

    // demux 侧状态
    bool streamPaused_ = false;
    bool demuxEof_ = false;
    bool backpressurePaused_ = false; // 快速推帧期间跳过背压控制

    bool audioDecoderPaused_ = false;
    bool videoDecoderPaused_ = false;
    bool decoderEof_ = false;

    bool isPrebuffering_ = false;
    bool isRebuffering_ = false;
    bool readyFired_ = false;

    int32_t pauseFailCount_ = 0;
    int32_t starvationCount_ = 0;

    std::optional<TimePoint> lastCheckTime_;
    std::optional<TimePoint> lastDecoderCheckTime_;
};

} // namespace slark
