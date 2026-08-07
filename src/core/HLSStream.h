//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

#include "Http1Session.h"
#include "HttpRequest.h"
#include "IStream.h"
#include "M3U8Parser.h"
#include "MediaPlaylist.h"
#include "Task.hpp"
#include "Thread.h"
#include "Timer.h"

namespace slark {

class HLSStream : public IStream, public std::enable_shared_from_this<HLSStream> {
 public:
    explicit HLSStream(std::shared_ptr<Thread> ownerThread);
    ~HLSStream() override;

    StreamResult<StreamInfo> open(const OpenParams& params) override;
    void close() noexcept override;
    void start() noexcept override;
    void pause() noexcept override;
    StreamResult<void> seek(uint64_t position) override;
    StreamResult<void> seekByTime(double timeSec) override;
    void updateRange(Range range) noexcept override;

    [[nodiscard]] StreamState state() const noexcept override {
        return state_.load();
    }

    [[nodiscard]] StreamInfo info() const noexcept override {
        std::lock_guard<std::mutex> lock(infoMutex_);
        return info_;
    }

    [[nodiscard]] uint64_t position() const noexcept override {
        return position_.load();
    }

    [[nodiscard]] bool isRunning() const noexcept override {
        return isRunning_.load();
    }

    [[nodiscard]] bool isCompleted() const noexcept override {
        return isCompleted_.load();
    }

    [[nodiscard]] std::shared_ptr<MediaPlaylist> playlist() const noexcept {
        return playlist_;
    }

    void setMetadataCallback(MetadataCallback callback) noexcept {
        metadataCallback_ = std::move(callback);
    }

 private:
    struct StreamControl {
        std::atomic<bool> alive{true};
    };

    struct PlaylistOp {
        uint64_t generation = 0;
        uint64_t operationId = 0;
        std::shared_ptr<http::HttpRequest> request;
        Task<void> task;
        std::shared_ptr<StreamControl> control;
    };

    struct SegmentOp {
        uint64_t generation = 0;
        uint64_t operationId = 0;
        uint32_t segIndex = 0;
        std::shared_ptr<http::HttpRequest> request;
        Task<void> task;
        std::shared_ptr<StreamControl> control;
    };

    struct PlaylistEvent {
        uint64_t generation = 0;
        uint64_t operationId = 0;
        http::HttpResponse response;
    };

    struct SegmentEvent {
        uint64_t generation = 0;
        uint64_t operationId = 0;
        uint32_t segIndex = 0;
        http::StreamChunk chunk;
    };

    std::optional<std::string> selectVariantUri(uint32_t bandwidth);

    void downloadSegments();
    void downloadSegment(uint32_t segIndex);
    void refreshPlaylist();
    void scheduleRetry(uint32_t segIndex);

    Task<void> fetchPlaylist(std::shared_ptr<http::HttpRequest> request,
                             http::RequestInfo info,
                             std::shared_ptr<StreamControl> control,
                             uint64_t generation,
                             uint64_t operationId);

    void onPlaylistEvent(PlaylistEvent event) noexcept;
    void onSegmentEvent(SegmentEvent event) noexcept;

    void handlePlaylistResponse(http::HttpResponse&& response);
    void handleSegmentChunk(http::StreamChunk&& chunk, uint32_t segmentIndex);
    void reportHttpError(const http::HttpResponse& response);
    void reportSegmentError(uint32_t segIndex, int32_t originCode);

    [[nodiscard]] std::string resolveUrl(const std::string& relativeUrl) const;

    void cancelPlaylistOp(uint64_t timeoutMs);
    void cancelSegmentOp(uint64_t timeoutMs);

    [[nodiscard]] bool onOwnerThread() const noexcept {
        return ownerThread_ && ownerThread_->getId() == std::this_thread::get_id();
    }

 private:
    std::shared_ptr<Thread> ownerThread_;
    mutable std::mutex mutex_;

    std::optional<PlaylistOp> playlistOp_;
    std::optional<SegmentOp> segmentOp_;
    std::shared_ptr<http::Http1Session> controlSession_;
    std::shared_ptr<http::Http1Session> mediaSession_;
    uint64_t generation_ = 0;
    uint64_t nextOperationId_ = 1;
    bool closing_ = false;
    bool started_ = false;
    uint32_t retryCount_ = 0;
    TimerId refreshTimerId_{};
    TimerId retryTimerId_{};

    std::unique_ptr<M3U8Parser> parser_;
    std::shared_ptr<MediaPlaylist> playlist_;
    OpenParams params_;

    std::string baseUrl_;
    std::string mediaPlaylistUrl_;
    std::atomic<uint32_t> currentSegmentIndex_{0};

    StreamInfo info_;
    mutable std::mutex infoMutex_;

    std::atomic<StreamState> state_{StreamState::Idle};
    std::atomic<uint64_t> position_{0};
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> isCompleted_{false};
    std::atomic<bool> isPaused_{false};
    std::atomic<int32_t> lastErrorOriginCode_{0};

    MetadataCallback metadataCallback_;
};

}  // namespace slark
