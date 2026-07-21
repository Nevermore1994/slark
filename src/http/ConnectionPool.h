//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "AsyncSocket.h"
#include "CancellationToken.h"
#include "Clock.h"
#include "Coroutine.hpp"
#include "HttpResponseParser.h"

#include <chrono>
#include <memory>
#include <string>

namespace slark::http {

// 持有连接的 RAII 包装，析构时自动 close
class PooledConnection {
public:
    PooledConnection(std::unique_ptr<IAsyncSocket> socket) : socket_(std::move(socket)) {}
    ~PooledConnection() { close(); }

    PooledConnection(PooledConnection&&) noexcept = default;
    PooledConnection& operator=(PooledConnection&&) noexcept = default;

    IAsyncSocket& operator*() noexcept { return *socket_; }
    IAsyncSocket* operator->() noexcept { return socket_.get(); }
    [[nodiscard]] IAsyncSocket* get() const noexcept { return socket_.get(); }
    [[nodiscard]] bool isValid() const noexcept { return socket_ && socket_->isValid(); }

    Coroutine<AsyncSocketResult> asyncSend(std::string_view data,
                                           std::chrono::milliseconds timeout) {
        co_return co_await socket_->asyncSend(data, timeout);
    }
    Coroutine<AsyncSocketResult> asyncSend(std::string_view data,
                                           std::chrono::milliseconds timeout,
                                           CancellationToken token) {
        co_return co_await socket_->asyncSend(data, timeout, token);
    }
    Coroutine<bool> asyncReceiveStreaming(HttpResponseParser& parser,
                                          std::chrono::milliseconds timeout,
                                          CancellationToken token = CancellationToken{}) {
        co_return co_await socket_->asyncReceiveStreaming(parser, timeout, token);
    }

    void close() {
        if (socket_) {
            socket_->close();
            socket_.reset();
        }
    }

    void setPerf(RequestPerf* perf) noexcept {
        if (socket_) { socket_->setPerf(perf); }
    }

private:
    std::unique_ptr<IAsyncSocket> socket_;
};

// 建立新连接（DNS + TCP + 可选 TLS）
Coroutine<std::unique_ptr<PooledConnection>> co_acquire_connection(
    std::string host,
    uint16_t port,
    std::string scheme,
    IPVersion ipVersion = IPVersion::V4,
    std::chrono::milliseconds connectTimeout = std::chrono::milliseconds{0},
    std::chrono::milliseconds tlsHandshakeTimeout = std::chrono::milliseconds{0},
    RequestPerf* perf = nullptr);

} // namespace slark::http
