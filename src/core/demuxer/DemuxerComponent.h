//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once
#include <mutex>
#include <optional>

#include "AudioInfo.h"
#include "Channel.hpp"
#include "DemuxerManager.h"
#include "IPipelineComponent.h"
#include "LoopThread.h"
#include "NonCopyable.h"
#include "VideoInfo.h"
#include "ThreadManager.h"
#include "perf/PlaySession.h"

namespace slark {

using HandleSeekFunc = std::function<void(Range, uint64_t)>;
using HandleDemuxResultFunc =
    std::function<void(const std::shared_ptr<IDemuxer>&, DemuxerResult&&)>;

class DemuxerComponent : public IPipelineComponent, public slark::NonCopyMovable {
 private:
    struct MediaHeader {
        std::optional<VideoInfo> video;
        std::optional<AudioInfo> audio;
        CTime duration;
    };

 public:
    explicit DemuxerComponent(ReceiverPtr<DataPacket> receiver, DemuxerConfig config);

    ~DemuxerComponent();

    void close() noexcept override;

    void stop() noexcept;

    void start() noexcept override;

    void pause() noexcept override;

    void resume() noexcept;

    void flush() noexcept override;

    [[nodiscard]] bool isRunning() const noexcept override {
        auto worker = worker_.load();
        return worker && !worker->hasExited();
    }

    void seekToPos(uint64_t pos) noexcept;

    void seekToTime(double time) noexcept;

    [[nodiscard]] bool isCurrentSourceGeneration(uint64_t generation) const noexcept {
        return generation == sourceGeneration_.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint64_t beginSourceOperation() noexcept {
        return sourceGeneration_.fetch_add(1, std::memory_order_acq_rel) + 1;
    }

    [[nodiscard]] uint64_t getSeekToPos(double time) noexcept;

    void setHandleResultFunc(HandleDemuxResultFunc&& func) noexcept {
        handleResultFunc_.reset(std::make_shared<HandleDemuxResultFunc>(std::move(func)));
    }

    void setHandleSeekFunc(HandleSeekFunc&& func) noexcept {
        handleSeekFunc_.reset(std::make_shared<HandleSeekFunc>(std::move(func)));
    }

    [[nodiscard]] uint32_t pendingCount() const noexcept {
        return receiver_ ? receiver_->size() : 0;
    }

    [[nodiscard]] bool hasVideo() const noexcept {
        auto header = header_.load();
        return header && header->video.has_value();
    }

    [[nodiscard]] bool hasAudio() const noexcept {
        auto header = header_.load();
        return header && header->audio.has_value();
    }

    [[nodiscard]] std::shared_ptr<VideoInfo> videoInfo() const noexcept {
        auto header = header_.load();
        return header && header->video ? std::make_shared<VideoInfo>(*header->video) : nullptr;
    }

    [[nodiscard]] std::shared_ptr<AudioInfo> audioInfo() const noexcept {
        auto header = header_.load();
        return header && header->audio ? std::make_shared<AudioInfo>(*header->audio) : nullptr;
    }

    [[nodiscard]] CTime totalDuration() const noexcept {
        auto header = header_.load();
        return header ? header->duration : CTime{};
    }

    void setDemuxer(std::shared_ptr<IDemuxer> demuxer) noexcept {
        if (demuxer) {
            demuxer_.reset(std::move(demuxer));
        } else {
            LogE("set demuxer is nullptr.");
        }
    }

 private:
    void reset() noexcept;
    bool open() noexcept;
    void runLoop(LoopThread& thread) noexcept;
    bool postToWorker(ThreadTask task) noexcept;
    bool runInDomain(ThreadTask task) noexcept;
    void publishHeader() noexcept;
    void seekToPosOnWorker(uint64_t pos, uint64_t generation) noexcept;

    void invokeHandleResultFunc(DemuxerResult&& result) noexcept {
        if (result.resultCode == DemuxerResultCode::ParsedHeader ||
            result.resultCode == DemuxerResultCode::VideoInfoChange ||
            result.resultCode == DemuxerResultCode::ParsedFPS) {
            publishHeader();
        }
        auto func = handleResultFunc_.load();
        if (func) {
            std::invoke(*func, demuxer_.load(), std::move(result));
        }
    }

    void invokeSeekFunc(Range range, uint64_t generation) noexcept {
        auto func = handleSeekFunc_.load();
        if (func) {
            std::invoke(*func, range, generation);
        }
    }

    bool createDemuxer() noexcept;

    bool openDemuxer(DataPacket& packet) noexcept;

 private:
    AtomicSharedPtr<LoopThread> worker_;
    std::mutex startMutex_;
    std::once_flag finalizeOnce_;
    ReceiverPtr<DataPacket> receiver_;
    DemuxerConfig config_;
    AtomicSharedPtr<HandleDemuxResultFunc> handleResultFunc_;
    AtomicSharedPtr<HandleSeekFunc> handleSeekFunc_;
    std::unique_ptr<Buffer> probeBuffer_;
    AtomicSharedPtr<IDemuxer> demuxer_;
    AtomicSharedPtr<const MediaHeader> header_;
    std::atomic_bool stopped_{false};

    std::atomic_bool flushed_ = false;
    std::atomic_bool paused_ = false;
    std::atomic_bool isClosed_ = false;
    std::atomic_bool pendingSeekVerification_{false};
    std::atomic<uint64_t> pendingSeekVerifiedOffset_{0};
    std::atomic<uint64_t> sourceGeneration_{0};
    std::weak_ptr<perf::PlaySession> session_;
};

}  // namespace slark
