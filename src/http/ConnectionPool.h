//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "AsyncSocket.h"
#include "CancellationToken.h"
#include "Clock.h"
#include "Task.hpp"

namespace slark::http {

// RAII wrapper owning a connection, closes it automatically on destruction
class PooledConnection {
 public:
    PooledConnection(std::unique_ptr<IAsyncSocket> socket) : socket_(std::move(socket)) {}

    ~PooledConnection() {
        close();
    }

    PooledConnection(PooledConnection&&) noexcept = default;
    PooledConnection& operator=(PooledConnection&&) noexcept = default;

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

    void close() {
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

// Establish a new connection (DNS + TCP + optional TLS). Cancellation at any stage
// terminates immediately and the connection is not recycled.
Task<std::unique_ptr<PooledConnection>> co_acquire_connection(
    std::string host,
    uint16_t port,
    std::string scheme,
    IPVersion ipVersion = IPVersion::V4,
    std::chrono::milliseconds connectTimeout = std::chrono::milliseconds{0},
    std::chrono::milliseconds tlsHandshakeTimeout = std::chrono::milliseconds{0},
    RequestPerf* perf = nullptr,
    CancellationToken token = {});

}  // namespace slark::http
