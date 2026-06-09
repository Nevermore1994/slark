//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once
#include "Channel.hpp"
#include "DemuxerManager.h"
#include "IPipelineComponent.h"
#include "NonCopyable.h"
#include "Thread.h"
#include "ThreadManager.h"
#include "perf/PlaySession.h"

namespace slark {

using HandleSeekFunc = std::function<void(Range)>;
using HandleDemuxResultFunc =
    std::function<void(const std::shared_ptr<IDemuxer>&, DemuxerResult&&)>;

class DemuxerComponent : public IPipelineComponent, public slark::NonCopyMovable {
private:
    template <typename Func, typename Default>
    auto withDemuxer(Func&& func, Default&& defaultVal) const noexcept {
        if (auto demuxer = demuxer_.load()) {
            return func(*demuxer);
        }
        LogE("demuxer is nullptr.");
        return std::forward<Default>(defaultVal);
    }

public:
    explicit DemuxerComponent(ReceiverPtr<DataPacket> receiver, DemuxerConfig config);

    ~DemuxerComponent();

    void reset() noexcept;

    bool open() noexcept;

    void close() noexcept override;

    /// 启动 worker 线程（生命周期接口，构造后调一次）
    void start() noexcept override;

    /// IO 侧 pause 由 MediaSourceManager 完成，本组件无需感知，空实现
    void pause() noexcept override {}

    void flush() noexcept override;

    [[nodiscard]] bool isRunning() const noexcept override {
        return worker_ && worker_->isRunning();
    }

    void seekToPos(uint64_t pos) noexcept;

    /// In HLS, what you get is the TS index, while in other cases, it's the file offset.
    [[nodiscard]] uint64_t getSeekToPos(double time) noexcept;

    void setHandleResultFunc(HandleDemuxResultFunc&& func) noexcept {
        handleResultFunc_.reset(std::make_shared<HandleDemuxResultFunc>(std::move(func)));
    }

    void setHandleSeekFunc(HandleSeekFunc&& func) noexcept {
        handleSeekFunc_.reset(std::make_shared<HandleSeekFunc>(std::move(func)));
    }

    [[nodiscard]] bool isOpen() const noexcept {
        return withDemuxer(
            [](auto& d) {
                return d.isOpened();
            },
            false);
    }

    [[nodiscard]] bool isCompleted() const noexcept {
        return withDemuxer(
            [](auto& d) {
                return d.isCompleted();
            },
            false);
    }

    [[nodiscard]] bool hasVideo() const noexcept {
        return withDemuxer(
            [](auto& d) {
                return d.hasVideo();
            },
            false);
    }

    [[nodiscard]] bool hasAudio() const noexcept {
        return withDemuxer(
            [](auto& d) {
                return d.hasAudio();
            },
            false);
    }

    [[nodiscard]] std::shared_ptr<VideoInfo> videoInfo() noexcept {
        return withDemuxer(
            [](auto& d) {
                return d.videoInfo();
            },
            std::shared_ptr<VideoInfo>{});
    }

    [[nodiscard]] std::shared_ptr<AudioInfo> audioInfo() noexcept {
        return withDemuxer(
            [](auto& d) {
                return d.audioInfo();
            },
            std::shared_ptr<AudioInfo>{});
    }

    [[nodiscard]] std::shared_ptr<DemuxerHeaderInfo> headerInfo() noexcept {
        return withDemuxer(
            [](auto& d) {
                return d.headerInfo();
            },
            std::shared_ptr<DemuxerHeaderInfo>{});
    }

    [[nodiscard]] CTime totalDuration() const noexcept {
        return withDemuxer(
            [](auto& d) {
                return d.totalDuration();
            },
            CTime{});
    }

    [[nodiscard]] std::shared_ptr<IDemuxer> demuxer() const noexcept {
        return demuxer_.load();
    }

    void setDemuxer(std::shared_ptr<IDemuxer> demuxer) noexcept {
        if (demuxer) {
            demuxer_.reset(std::move(demuxer));
        } else {
            LogE("set demuxer is nullptr.");
        }
    }

    [[nodiscard]] DemuxerType type() const noexcept {
        return withDemuxer(
            [](auto& d) {
                return d.type();
            },
            DemuxerType::Unknown);
    }

private:
    void runLoop() noexcept;

    void invokeHandleResultFunc(DemuxerResult&& result) noexcept {
        auto func = handleResultFunc_.load();
        if (func) {
            std::invoke(*func, demuxer_.load(), std::move(result));
        }
    }

    void invokeSeekFunc(Range range) noexcept {
        auto func = handleSeekFunc_.load();
        if (func) {
            std::invoke(*func, range);
        }
    }

    bool createDemuxer() noexcept;

    bool openDemuxer(DataPacket& packet) noexcept;

private:
    std::shared_ptr<Thread> worker_;
    ReceiverPtr<DataPacket> receiver_;
    DemuxerConfig config_;
    AtomicSharedPtr<HandleDemuxResultFunc> handleResultFunc_;
    AtomicSharedPtr<HandleSeekFunc> handleSeekFunc_;
    std::unique_ptr<Buffer> probeBuffer_;
    AtomicSharedPtr<IDemuxer> demuxer_;
    std::atomic_bool flushed_ = false;
    std::atomic_bool isClosed_ = false;
    std::atomic_bool stopRequested_ = false;
    std::atomic<uint64_t> pendingSeekVerifiedOffset_{0};
    std::weak_ptr<perf::PlaySession> session_;
};

} // namespace slark
