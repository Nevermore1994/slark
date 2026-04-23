//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "HttpRequest.h"
#include "IStream.h"
#include "MediaPlaylist.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <queue>

namespace slark {

class HLSStream : public IStream, public std::enable_shared_from_this<HLSStream> {
public:
    HLSStream();
    ~HLSStream() override;

    StreamResult<StreamInfo> open(const OpenParams& params) override;
    void close() noexcept override;
    void start() noexcept override;
    void pause() noexcept override;
    StreamResult<void> seek(uint64_t position) override;
    void updateRange(Range range) noexcept override;

    [[nodiscard]] StreamState state() const noexcept override {
        return state_.load();
    }

    [[nodiscard]] const StreamInfo& info() const noexcept override {
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
    StreamResult<void> parseMasterPlaylist(const std::string& content);
    StreamResult<void> parseMediaPlaylist(const std::string& content);
    std::optional<std::string> selectVariantUri(uint32_t bandwidth);

    void downloadSegments();
    void downloadSegment(const Segment& segment);
    void refreshPlaylist();
    StreamResult<void> parseMediaPlaylistIncremental(const std::string& content);

    void handlePlaylistResponse(http::HttpResponse&& response);
    void handleSegmentChunk(http::StreamChunk&& chunk, uint32_t segmentIndex);

    [[nodiscard]] std::string resolveUrl(const std::string& relativeUrl) const;

private:
    std::shared_ptr<MediaPlaylist> playlist_;
    std::shared_ptr<http::HttpRequest> request_;
    OpenParams params_;

    std::string baseUrl_;
    std::string mediaPlaylistUrl_;
    std::atomic<uint32_t> currentVariantIndex_{0};
    std::atomic<uint32_t> currentSegmentIndex_{0};

    StreamInfo info_;

    std::atomic<StreamState> state_{StreamState::Idle};
    std::atomic<uint64_t> position_{0};
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> isCompleted_{false};
    std::atomic<bool> isPaused_{false};
    bool isLiveRefresh_ = false; // live 刷新模式，只追加新分片

    MetadataCallback metadataCallback_;
    std::mutex mutex_;
};

} // namespace slark
