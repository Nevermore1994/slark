//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "ConnectionPool.h"

#include "DnsResolver.h"
#include "IOContext.h"
#include "Log.hpp"

#include <netdb.h>

namespace slark::http {

ConnectionPool& ConnectionPool::instance() noexcept {
    static ConnectionPool instance;
    return instance;
}

ConnectionPool::ConnectionPool(const Config& config)
    : config_(config), lastCleanupTime_(Time::nowTimeStamp()) {
    // 确保 IOContext 先于 ConnectionPool 构造
    // 这样 ConnectionPool 先于 IOContext 析构，避免析构时访问已销毁的 mutex
    (void)IOContext::instance();
}

ConnectionPool::~ConnectionPool() {
    clear();
}

size_t ConnectionPool::getShardIndex(const std::string& key) const noexcept {
    // 简单的hash分片
    size_t hash = 0;
    for (char c : key) {
        hash = hash * 31 + static_cast<size_t>(c);
    }
    return hash % kShardCount;
}

std::string ConnectionPool::makeKey(const std::string& host,
                                    uint16_t port,
                                    const std::string& scheme) const {
    // IPv6 地址需要用方括号包裹
    bool isIPv6 = host.find(':') != std::string::npos;
    if (isIPv6) {
        return scheme + "://[" + host + "]:" + std::to_string(port);
    }
    return scheme + "://" + host + ":" + std::to_string(port);
}

std::string ConnectionPool::extractHost(const std::string& key) {
    auto schemeEnd = key.find("://");
    if (schemeEnd == std::string::npos) {
        return {};
    }
    auto hostStart = schemeEnd + 3;

    // IPv6 地址用方括号包裹
    if (hostStart < key.size() && key[hostStart] == '[') {
        auto closeBracket = key.find(']', hostStart);
        if (closeBracket == std::string::npos) {
            return {};
        }
        return key.substr(hostStart + 1, closeBracket - hostStart - 1);
    }

    // IPv4 或主机名
    auto colonPos = key.find(':', hostStart);
    if (colonPos == std::string::npos) {
        return {};
    }
    return key.substr(hostStart, colonPos - hostStart);
}

void ConnectionPool::closeConnection(ConnectionInfo& info) {
    if (info.socket) {
        info.socket->close();
    }
    info.isValid = false;
}

void ConnectionPool::eraseIf(const std::function<bool(const ConnectionInfo&)>& predicate) {
    for (auto& shard : shards_) {
        std::lock_guard<std::mutex> lock(shard.mutex);
        for (auto& [key, deque] : shard.pool) {
            std::erase_if(deque, [&predicate](const auto& connInfo) {
                if (predicate(*connInfo)) {
                    closeConnection(*connInfo);
                    return true;
                }
                return false;
            });
        }
    }
}

std::unique_ptr<IAsyncSocket> ConnectionPool::tryReuse(const std::string& key, RequestPerf* perf) {
    auto shardIndex = getShardIndex(key);
    std::lock_guard<std::mutex> lock(shards_[shardIndex].mutex);
    auto& pool = shards_[shardIndex].pool;

    if (auto it = pool.find(key); it != pool.end()) {
        auto& deque = it->second;
        while (!deque.empty()) {
            auto connInfo = std::move(deque.back());
            deque.pop_back();
            if (!connInfo->isValid || !connInfo->socket || !connInfo->socket->isValid()) {
                continue;
            }
            // 非阻塞 peek 检测对端是否已发 FIN
            char buf;
            ssize_t n = ::recv(connInfo->socket->getFd(), &buf, 1, MSG_PEEK | MSG_DONTWAIT);
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                connInfo->socket->close();
                continue;
            }
            if (perf) {
                perf->connection.reuseAttempted = true;
                perf->connection.reuseSucceeded = true;
            }
            return std::move(connInfo->socket);
        }
    }
    if (perf) {
        perf->connection.reuseAttempted = true;
        perf->connection.reuseSucceeded = false;
    }
    return nullptr;
}

Coroutine<std::unique_ptr<IAsyncSocket>> ConnectionPool::connectNew(
    const std::string& host,
    uint16_t port,
    IPVersion ipVersion,
    std::chrono::milliseconds timeout,
    RequestPerf* perf) {
    auto socket = std::make_unique<AsyncSocket>(ipVersion);
    if (!socket) {
        co_return nullptr;
    }

    auto addrInfo =
        co_await DnsResolver::instance().resolveAsync(host, std::to_string(port), ipVersion);
    if (!addrInfo) {
        LogW("DNS resolution failed for {}", host);
        co_return nullptr;
    }

    try {
        socket->setPerf(perf);
        co_await socket->asyncConnect(addrInfo, timeout);
        co_return socket;
    } catch (const TimeoutException&) {
        LogW("Connect timeout for {}", host);
        if (perf) {
            perf->transport.timeoutSource = TimeoutSource::Connect;
        }
        co_return nullptr;
    } catch (const std::exception& e) {
        LogW("Connect failed: {}", e.what());
        DnsResolver::instance().invalidate(host);
        co_return nullptr;
    }
}

#if ENABLE_HTTPS
Coroutine<void> ConnectionPool::upgradeTls(std::unique_ptr<IAsyncSocket>& socket,
                                           const std::string& host,
                                           std::chrono::milliseconds connectTimeout,
                                           std::chrono::milliseconds handshakeTimeout,
                                           RequestPerf* perf) {
    auto tlsTransport = std::make_unique<TLSTransport>(socket->getFd(), host);
    // 使用专门的握手超时，若未指定则回退到连接超时
    auto timeout = handshakeTimeout.count() > 0 ? handshakeTimeout : connectTimeout;
    try {
        co_await tlsTransport->performHandshake(timeout);
        socket->setTransport(std::move(tlsTransport));
    } catch (const TimeoutException&) {
        LogW("TLS handshake timeout for {}", host);
        if (perf) {
            perf->transport.timeoutSource = TimeoutSource::TlsHandshake;
        }
        throw;
    } catch (const std::exception& e) {
        LogW("TLS handshake failed: {}", e.what());
        throw;
    }
}
#endif

Coroutine<std::unique_ptr<IAsyncSocket>> ConnectionPool::acquire(
    const std::string& host,
    uint16_t port,
    const std::string& scheme,
    IPVersion ipVersion,
    std::chrono::milliseconds connectTimeout,
    std::chrono::milliseconds tlsHandshakeTimeout,
    RequestPerf* perf) {
    // 仅在节流间隔到期时触发清理，且不阻塞 acquire 主路径
    tryScheduleCleanup();

    auto key = makeKey(host, port, scheme);
    if (perf) {
        perf->host = host;
    }

    // 1. 尝试复用现有连接
    if (auto socket = tryReuse(key, perf)) {
        co_return socket;
    }

    // 2. 建立新连接
    auto timeout = connectTimeout.count() > 0 ? connectTimeout : config_.connectTimeout;
    auto connectStart = Time::nowTimeStamp();
    auto socket = co_await connectNew(host, port, ipVersion, timeout, perf);
    auto connectEnd = Time::nowTimeStamp();

    if (perf) {
        perf->connection.connectTimeMs = (connectEnd - connectStart).toMilliSeconds().count();
        perf->connection.connectResult =
            socket ? ResultCode::Success : ResultCode::ConnectAddressError;
    }

    if (!socket) {
        co_return nullptr;
    }

    // 3. HTTPS: 执行 TLS 升级
#if ENABLE_HTTPS
    if (scheme == "https") {
        try {
            auto tlsStart = Time::nowTimeStamp();
            co_await upgradeTls(socket, host, timeout, tlsHandshakeTimeout, perf);
            auto tlsEnd = Time::nowTimeStamp();
            if (perf) {
                perf->connection.tlsHandshakeTimeMs = (tlsEnd - tlsStart).toMilliSeconds().count();
            }
        } catch (const std::exception& e) {
            LogW("TLS handshake failed: {}", e.what());
            if (perf) {
                perf->connection.connectResult = ResultCode::Failed;
            }
            co_return nullptr;
        }
    }
#endif

    co_return socket;
}

void ConnectionPool::release(std::unique_ptr<IAsyncSocket> socket,
                             const std::string& host,
                             uint16_t port,
                             const std::string& scheme) {
    if (!socket || !socket->isValid()) {
        return;
    }

    auto key = makeKey(host, port, scheme);
    auto shardIndex = getShardIndex(key);
    std::lock_guard<std::mutex> lock(shards_[shardIndex].mutex);
    auto& pool = shards_[shardIndex].pool;
    auto& deque = pool[key];

    // LRU淘汰：池满时移除最久未使用的连接（front）
    if (deque.size() >= config_.maxConnectionsPerHost) {
        closeConnection(*deque.front());
        deque.pop_front();
    }

    auto connInfo = std::make_unique<ConnectionInfo>();
    connInfo->socket = std::move(socket);
    connInfo->host = host;
    connInfo->port = port;
    connInfo->scheme = scheme;
    connInfo->lastUsedTime = Time::nowTimeStamp();
    connInfo->isValid = true;
    deque.push_back(std::move(connInfo));
}

void ConnectionPool::clear() {
    for (auto& shard : shards_) {
        std::lock_guard<std::mutex> lock(shard.mutex);
        for (auto& [key, deque] : shard.pool) {
            for (auto& connInfo : deque) {
                closeConnection(*connInfo);
            }
        }
        shard.pool.clear();
    }
}

void ConnectionPool::clear(const std::string& host) {
    for (auto& shard : shards_) {
        std::lock_guard<std::mutex> lock(shard.mutex);
        std::erase_if(shard.pool, [&host](auto& entry) {
            if (extractHost(entry.first) != host) {
                return false;
            }
            for (auto& connInfo : entry.second) {
                closeConnection(*connInfo);
            }
            return true;
        });
    }
}

void ConnectionPool::removeInvalidConnections() {
    eraseIf([](const ConnectionInfo& info) {
        return !info.isValid || !info.socket || !info.socket->isValid();
    });
}

size_t ConnectionPool::size() const noexcept {
    size_t total = 0;
    for (const auto& shard : shards_) {
        std::lock_guard<std::mutex> lock(shard.mutex);
        for (const auto& [key, deque] : shard.pool) {
            total += deque.size();
        }
    }
    return total;
}

size_t ConnectionPool::size(const std::string& host) const noexcept {
    size_t total = 0;
    for (const auto& shard : shards_) {
        std::lock_guard<std::mutex> lock(shard.mutex);
        for (const auto& [key, deque] : shard.pool) {
            if (extractHost(key) == host) {
                total += deque.size();
            }
        }
    }
    return total;
}

void ConnectionPool::cleanupIdleConnections() {
    auto now = Time::nowTimeStamp();

    // 收集需要关闭的连接，缩小锁作用域
    std::vector<std::unique_ptr<ConnectionInfo>> expired;

    for (auto& shard : shards_) {
        std::vector<std::pair<std::string, std::unique_ptr<ConnectionInfo>>> shardExpired;
        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            for (auto& [key, deque] : shard.pool) {
                std::erase_if(deque, [&now, &shardExpired, this, &key](auto& connInfo) {
                    auto elapsed = (now - connInfo->lastUsedTime).toMilliSeconds();
                    if (elapsed > config_.idleTimeout) {
                        shardExpired.emplace_back(key, std::move(connInfo));
                        return true;
                    }
                    return false;
                });
            }
        }

        // 在锁外关闭过期连接
        for (auto& [k, info] : shardExpired) {
            closeConnection(*info);
        }
    }

    // 更新最后清理时间
    {
        std::lock_guard<std::mutex> lock(cleanupMutex_);
        lastCleanupTime_ = now;
    }
}

void ConnectionPool::tryScheduleCleanup() {
    auto now = Time::nowTimeStamp();
    {
        std::lock_guard<std::mutex> lock(cleanupMutex_);
        if (now - lastCleanupTime_ < Time::TimeDelta::fromMilliSeconds(std::chrono::seconds(30))) {
            return;
        }
    }

    // 原子门闩：使用compare_exchange确保只有一个线程执行清理
    bool expected = false;
    if (!cleanupInProgress_.compare_exchange_strong(expected, true)) {
        return; // 另一个线程正在执行清理
    }

    if (cleanupThread_ && cleanupThread_->joinable()) {
        // 上次清理仍在进行中，跳过本次，避免 join 阻塞 acquire 路径
        if (cleanupInProgress_.load()) {
            cleanupInProgress_.store(false);
            return;
        }
        cleanupThread_->join();
    }
    cleanupThread_.emplace([this] {
        cleanupIdleConnections();
        cleanupInProgress_.store(false);
    });
}

PooledConnection::PooledConnection(std::unique_ptr<IAsyncSocket> socket,
                                   const std::string& host,
                                   uint16_t port,
                                   const std::string& scheme)
    : socket_(std::move(socket)), host_(host), port_(port), scheme_(scheme) {}

PooledConnection::~PooledConnection() {
    if (socket_ && socket_->isValid()) {
        ConnectionPool::instance().release(std::move(socket_), host_, port_, scheme_);
    }
}

} // namespace slark::http
