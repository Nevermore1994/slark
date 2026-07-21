//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Coroutine.hpp"
#include "HttpRequest.h"
#include "IStream.h"
#include "StringUtil.h"

#include <atomic>
#include <memory>

namespace slark {

class HttpStream : public IStream, public std::enable_shared_from_this<HttpStream> {
public:
    HttpStream();
    ~HttpStream() override;

    auto open(const OpenParams& params) -> StreamResult<StreamInfo> override;
    void close() noexcept override;
    void start() noexcept override;
    void pause() noexcept override;
    auto seek(uint64_t position) -> StreamResult<void> override;
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

    [[nodiscard]] int32_t lastErrorOriginCode() const noexcept {
        return lastErrorOriginCode_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::string host() const noexcept {
        return StringUtil::extractHostFromUri(params_.uri);
    }

private:
    void handleChunk(http::StreamChunk&& chunk) noexcept;
    void handleError(const http::ErrorInfo& error) noexcept;

private:
    std::shared_ptr<http::HttpRequest> request_;
    OpenParams params_;
    Range readRange_;
    uint64_t contentLength_ = 0;
    bool acceptRanges_ = false;

    Coroutine<void> streamCoroutine_;

    StreamInfo info_;

    std::atomic<StreamState> state_{StreamState::Idle};
    std::atomic<uint64_t> position_{0};
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> isCompleted_{false};
    std::atomic<bool> isPaused_{false};
    std::atomic<int32_t> lastErrorOriginCode_{0};
};

} // namespace slark
