//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <chrono>
#include <expected>
#include <memory>
#include <string>

#include "AsyncSocket.h"
#include "CancellationToken.h"
#include "Clock.h"
#include "Task.hpp"

namespace slark::http {

// RAII wrapper owning a connection, closes it automatically on destruction
class OwnedConnection {
 public:
    OwnedConnection(std::unique_ptr<IAsyncSocket> socket) : socket_(std::move(socket)) {}

    ~OwnedConnection() {
        close();
    }

    OwnedConnection(OwnedConnection&&) noexcept = default;
    OwnedConnection& operator=(OwnedConnection&&) noexcept = default;

    IAsyncSocket& operator*() noexcept {
        return *socket_;
    }

    IAsyncSocket* operator->() noexcept {
        return socket_.get();
    }

    [[nodiscard]] IAsyncSocket* get() const noexcept {
        return socket_.get();
    }

    [[nodiscard]] bool isValid() const noexcept {
        return socket_ && socket_->isValid();
    }

    Task<AsyncSocketResult> asyncSend(std::string_view data, std::chrono::milliseconds timeout) {
        co_return co_await socket_->asyncSend(data, timeout);
    }

    Task<AsyncSocketResult> asyncSend(std::string_view data,
                                      std::chrono::milliseconds timeout,
                                      CancellationToken token) {
        co_return co_await socket_->asyncSend(data, timeout, token);
    }

    void close() noexcept {
        if (socket_) {
            socket_->close();
            socket_.reset();
        }
    }

    void setPerf(RequestPerf* perf) noexcept {
        if (socket_) {
            socket_->setPerf(perf);
        }
    }

 private:
    std::unique_ptr<IAsyncSocket> socket_;
};

using OpenConnectionResult = std::expected<std::unique_ptr<OwnedConnection>, ErrorInfo>;

// Floor for a single connect attempt so one budget slice still has a chance
// against a slow-but-alive path.
inline constexpr std::chrono::milliseconds kMinConnectAttemptBudget{1000};

// Splits the remaining connect budget across the pending candidates with a floor,
// so one unreachable address cannot starve the rest (RFC 8305-lite).
inline std::chrono::milliseconds connectAttemptBudget(std::chrono::milliseconds remaining,
                                                      size_t candidatesLeft) noexcept {
    if (candidatesLeft == 0) {
        candidatesLeft = 1;
    }
    auto budget = remaining / candidatesLeft;
    if (budget < kMinConnectAttemptBudget) {
        budget = kMinConnectAttemptBudget;
    }
    if (budget > remaining) {
        budget = remaining;
    }
    return budget;
}

// Establish a new connection (DNS + TCP + optional TLS). The returned connection is
// owned by the caller and closes automatically when it is destroyed.
Task<OpenConnectionResult> openConnection(
    std::string host,
    uint16_t port,
    std::string scheme,
    IPVersion ipVersion = IPVersion::V4,
    NetworkAddressPolicy addressPolicy = NetworkAddressPolicy::AllowAny,
    std::chrono::milliseconds connectTimeout = std::chrono::milliseconds{0},
    std::chrono::milliseconds tlsHandshakeTimeout = std::chrono::milliseconds{0},
    RequestPerf* perf = nullptr,
    CancellationToken token = {});

}  // namespace slark::http
