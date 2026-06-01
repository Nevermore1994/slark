//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "HttpRequest.h"
#include "IStream.h"
#include "M3U8Parser.h"
#include "MediaPlaylist.h"

#include <atomic>
#include <memory>
#include <mutex>

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
    StreamResult<void> seekByTime(double timeSec) override;
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
    std::optional<std::string> selectVariantUri(uint32_t bandwidth);

    void downloadSegments();
    void downloadSegment(uint32_t segIndex);
    void downloadSegmentWithRetry(uint32_t segIndex, int32_t retryLeft);
    void refreshPlaylist();

    void handlePlaylistResponse(http::HttpResponse&& response);
    void handleSegmentChunk(http::StreamChunk&& chunk, uint32_t segmentIndex);

    [[nodiscard]] std::string resolveUrl(const std::string& relativeUrl) const;

private:
    std::unique_ptr<M3U8Parser> parser_;
    std::shared_ptr<MediaPlaylist> playlist_;
    std::shared_ptr<http::HttpRequest> request_;
    OpenParams params_;

    std::string baseUrl_;
    std::string mediaPlaylistUrl_;
    std::atomic<uint32_t> currentSegmentIndex_{0};

    StreamInfo info_;

    std::atomic<StreamState> state_{StreamState::Idle};
    std::atomic<uint64_t> position_{0};
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> isCompleted_{false};
    std::atomic<bool> isPaused_{false};

    MetadataCallback metadataCallback_;
    std::mutex mutex_;
};

} // namespace slark
