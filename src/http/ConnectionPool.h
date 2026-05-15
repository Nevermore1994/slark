//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AsyncSocket.h"
#include "Clock.h"
#include "Coroutine.hpp"
#include "HttpResponseParser.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace slark::http {

struct ConnectionInfo {
    std::unique_ptr<IAsyncSocket> socket;
    std::string host;
    uint16_t port;
    std::string scheme;
    Time::TimePoint lastUsedTime;
    bool isValid = true;
};

class ConnectionPool : public NonCopyable {
public:
    struct Config {
        size_t maxConnectionsPerHost;
        std::chrono::milliseconds idleTimeout;
        std::chrono::milliseconds connectTimeout;
        std::chrono::milliseconds tlsHandshakeTimeout;
        std::chrono::milliseconds writeTimeout;
        std::chrono::milliseconds readIdleTimeout;

        Config()
            : maxConnectionsPerHost(10),
              idleTimeout(std::chrono::seconds(30)),
              connectTimeout(std::chrono::seconds(10)),
              tlsHandshakeTimeout(std::chrono::seconds(10)),
              writeTimeout(std::chrono::seconds(30)),
              readIdleTimeout(std::chrono::seconds(30)) {}
    };

    static ConnectionPool& instance() noexcept;

    explicit ConnectionPool(const Config& config = Config{});
    ~ConnectionPool();

    Coroutine<std::unique_ptr<IAsyncSocket>> acquire(
        const std::string& host,
        uint16_t port,
        const std::string& scheme,
        IPVersion ipVersion = IPVersion::V4,
        std::chrono::milliseconds connectTimeout = std::chrono::milliseconds{0},
        std::chrono::milliseconds tlsHandshakeTimeout = std::chrono::milliseconds{0},
        RequestPerf* perf = nullptr);

    void release(std::unique_ptr<IAsyncSocket> socket,
                 const std::string& host,
                 uint16_t port,
                 const std::string& scheme);

    void clear();
    void clear(const std::string& host);
    void removeInvalidConnections();

    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] size_t size(const std::string& host) const noexcept;

private:
    using ConnectionDeque = std::deque<std::unique_ptr<ConnectionInfo>>;

    // 分片锁实现
    static constexpr size_t kShardCount = 16;

    struct Shard {
        mutable std::mutex mutex;
        std::unordered_map<std::string, ConnectionDeque> pool;
    };

    std::array<Shard, kShardCount> shards_;

    // 根据 key 计算分片索引
    [[nodiscard]] size_t getShardIndex(const std::string& key) const noexcept;

    std::string makeKey(const std::string& host, uint16_t port, const std::string& scheme) const;
    static std::string extractHost(const std::string& key);
    static void closeConnection(ConnectionInfo& info);
    void cleanupIdleConnections();
    void tryScheduleCleanup();
    void eraseIf(const std::function<bool(const ConnectionInfo&)>& predicate);

    // 连接获取流程分层
    [[nodiscard]] std::unique_ptr<IAsyncSocket> tryReuse(const std::string& key, RequestPerf* perf);
    Coroutine<std::unique_ptr<IAsyncSocket>> connectNew(const std::string& host,
                                                        uint16_t port,
                                                        IPVersion ipVersion,
                                                        std::chrono::milliseconds timeout,
                                                        RequestPerf* perf = nullptr);
#if ENABLE_HTTPS
    Coroutine<void> upgradeTls(std::unique_ptr<IAsyncSocket>& socket,
                               const std::string& host,
                               std::chrono::milliseconds connectTimeout,
                               std::chrono::milliseconds handshakeTimeout,
                               RequestPerf* perf = nullptr);
#endif

    Config config_;
    mutable std::mutex cleanupMutex_;
    std::atomic<bool> cleanupInProgress_{false};
    Time::TimePoint lastCleanupTime_;
    std::optional<std::thread> cleanupThread_;
};

class PooledConnection {
public:
    PooledConnection(std::unique_ptr<IAsyncSocket> socket,
                     const std::string& host,
                     uint16_t port,
                     const std::string& scheme);
    ~PooledConnection();

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
        }
    }

    void setPerf(RequestPerf* perf) noexcept {
        if (socket_) {
            socket_->setPerf(perf);
        }
    }

private:
    std::unique_ptr<IAsyncSocket> socket_;
    std::string host_;
    uint16_t port_;
    std::string scheme_;
};

inline Coroutine<std::unique_ptr<PooledConnection>> co_acquire_connection(
    const std::string& host,
    uint16_t port,
    const std::string& scheme,
    IPVersion ipVersion = IPVersion::V4,
    std::chrono::milliseconds connectTimeout = std::chrono::milliseconds{0},
    std::chrono::milliseconds tlsHandshakeTimeout = std::chrono::milliseconds{0},
    RequestPerf* perf = nullptr) {
    auto socket =
        co_await ConnectionPool::instance()
            .acquire(host, port, scheme, ipVersion, connectTimeout, tlsHandshakeTimeout, perf);
    if (!socket) {
        co_return nullptr;
    }
    co_return std::make_unique<PooledConnection>(std::move(socket), host, port, scheme);
}

} // namespace slark::http
