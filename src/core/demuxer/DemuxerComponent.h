//
// Created by Nevermore- on 2025/7/6.
//
#pragma once
#include "Thread.h"
#include "NonCopyable.h"
#include "DemuxerManager.h"
#include <list>

namespace slark {

using HandleSeekFunc = std::function<void(Range)>;
using HandleDemuxResultFunc = std::function<void(const std::shared_ptr<IDemuxer>&, DemuxerResult&&)>;

class DemuxerComponent: public slark::NonCopyable {

public:
    explicit DemuxerComponent(DemuxerConfig config);

    ~DemuxerComponent() override;

    void reset() noexcept;

    bool open() noexcept;

    void close() noexcept;

    void start() noexcept;

    void pause() noexcept;

    void seekToPos(uint64_t pos) noexcept;

    void flush() noexcept {
        flushed_ = true;
    }

    ///In HLS, what you get is the TS index, while in other cases, it’s the file offset.
    [[nodiscard]] uint64_t getSeekToPos(double time) noexcept;

    bool pushData(std::list<DataPacket>&& data) noexcept;

    void setHandleResultFunc(HandleDemuxResultFunc&& func) noexcept {
        handleResultFunc_.reset(std::make_shared<HandleDemuxResultFunc>(std::move(func)));
    }

    void setHandleSeekFunc(HandleSeekFunc&& func) noexcept {
        handleSeekFunc_.reset(std::make_shared<HandleSeekFunc>(std::move(func)));
    }

    [[nodiscard]] bool isRunning() const noexcept {
        return worker_.isRunning();
    }

    [[nodiscard]] bool isOpen() const noexcept {
        if (auto demuxer = demuxer_.load()) {
            return demuxer->isOpened();
        } else {
            LogE("demuxer is nullptr.");
        }
        return false;
    }

    [[nodiscard]] bool isCompleted() const noexcept {
        if (auto demuxer = demuxer_.load()) {
            return demuxer->isCompleted();
        } else {
            LogE("demuxer is nullptr.");
        }
        return true;
    }

    [[nodiscard]] bool hasVideo() const noexcept {
        if (auto demuxer = demuxer_.load()) {
            return demuxer->hasVideo();
        } else {
            LogE("demuxer is nullptr.");
        }
        return false;
    }

    [[nodiscard]] bool hasAudio() const noexcept {
        if (auto demuxer = demuxer_.load()) {
            return demuxer->hasAudio();
        } else {
            LogE("demuxer is nullptr.");
        }
        return false;
    }

    [[nodiscard]] std::shared_ptr<VideoInfo> videoInfo() noexcept {
        if (auto demuxer = demuxer_.load()) {
            return demuxer->videoInfo();
        } else {
            LogE("demuxer is nullptr.");
        }
        return nullptr;
    }

    [[nodiscard]] std::shared_ptr<AudioInfo> audioInfo() noexcept {
        if (auto demuxer = demuxer_.load()) {
            return demuxer->audioInfo();
        } else {
            LogE("demuxer is nullptr.");
        }
        return nullptr;
    }

    [[nodiscard]] std::shared_ptr<DemuxerHeaderInfo> headerInfo() noexcept {
        if (auto demuxer = demuxer_.load()) {
            return demuxer->headerInfo();
        } else {
            LogE("demuxer is nullptr.");
        }
        return nullptr;
    }

    [[nodiscard]] CTime totalDuration() const noexcept {
        if (auto demuxer = demuxer_.load()) {
            return demuxer->totalDuration();
        } else {
            LogE("demuxer is nullptr.");
        }
        return {};
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
        if (auto demuxer = demuxer_.load()) {
            return demuxer->type();
        } else {
            LogE("demuxer is nullptr.");
        }
        return DemuxerType::Unknown;
    }
private:
    void demuxData() noexcept;

    void handleOpenMp4DemuxerResult(bool isSuccess) noexcept;

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

    void clearData() noexcept {
        dataList_.withLock([](auto& list) {
            list.clear();
        });
        flushed_ = false;
    }
private:
    Thread worker_;
    DemuxerConfig config_;
    AtomicSharedPtr<HandleDemuxResultFunc> handleResultFunc_;
    AtomicSharedPtr<HandleSeekFunc> handleSeekFunc_;
    Synchronized<std::list<DataPacket>> dataList_;
    std::unique_ptr<Buffer> probeBuffer_;
    AtomicSharedPtr<IDemuxer> demuxer_;
    std::atomic_bool flushed_ = false;
    std::atomic_bool isClosed_ = false;
};

} // slark
