//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "ConnectionPool.h"

#include "DnsResolver.h"
#include "IOContext.h"
#include "Log.hpp"

namespace slark::http {

static constexpr std::chrono::milliseconds kDefaultTlsTimeout{std::chrono::seconds{10}};

namespace {

// DNS 解析 + TCP 连接，失败返回 false（perf 已记录错误与耗时）
Task<bool> resolveAndConnect(AsyncSocket& socket,
                             const std::string& host,
                             uint16_t port,
                             IPVersion ipVersion,
                             std::chrono::milliseconds connectTimeout,
                             std::chrono::milliseconds& connectMs,
                             RequestPerf* perf,
                             CancellationToken& token) {
    auto addrInfo =
        co_await DnsResolver::instance().resolveAsync(host, std::to_string(port), ipVersion, token);
    if (token.isCancelled()) {
        co_return false;
    }
    if (!addrInfo) {
        LogW("[co_acquire_connection] DNS resolution failed for {}", host);
        co_return false;
    }

    auto ct = connectTimeout.count() > 0 ? connectTimeout : kDefaultConnectTimeout;
    auto connectStart = Time::nowTimeStamp();
    try {
        socket.setPerf(perf);
        co_await socket.asyncConnect(addrInfo, ct, token);
    } catch (const CancelledException&) {
        LogW("[co_acquire_connection] Connect cancelled for {}", host);
        co_return false;
    } catch (const TimeoutException&) {
        LogW("[co_acquire_connection] Connect timeout for {}", host);
        if (perf) {
            perf->transport.timeoutSource = TimeoutSource::Connect;
        }
        co_return false;
    } catch (const std::exception& e) {
        LogW("[co_acquire_connection] Connect failed: {} {}", host, e.what());
        DnsResolver::instance().invalidate(host);
        co_return false;
    }
    if (token.isCancelled()) {
        co_return false;
    }
    connectMs = (Time::nowTimeStamp() - connectStart).toMilliSeconds();
    if (perf) {
        perf->connection.connectTimeMs = connectMs.count();
    }
    LogI("[co_acquire_connection] connected: host={} port={}", host, port);
    co_return true;
}

// TLS 握手，失败返回 false（perf 已记录错误与耗时）
Task<bool> performTlsHandshake(AsyncSocket& socket,
                               const std::string& host,
                               std::chrono::milliseconds tlsHandshakeTimeout,
                               RequestPerf* perf,
                               CancellationToken& token) {
#if ENABLE_HTTPS
    auto tlsTransport = std::make_unique<TLSTransport>(socket.getFd(), host);
    auto tlsTimeout = tlsHandshakeTimeout.count() > 0 ? tlsHandshakeTimeout : kDefaultTlsTimeout;
    auto tlsStart = Time::nowTimeStamp();
    bool tlsOk = co_await tlsTransport->performHandshake(tlsTimeout, token);
    auto tlsEnd = Time::nowTimeStamp();

    if (perf) {
        perf->connection.tlsHandshakeTimeMs = (tlsEnd - tlsStart).toMilliSeconds().count();
    }
    if (token.isCancelled()) {
        co_return false;
    }
    if (!tlsOk) {
        LogW("[co_acquire_connection] TLS handshake failed: host={}", host);
        if (perf) {
            perf->transport.timeoutSource = TimeoutSource::TlsHandshake;
        }
        co_return false;
    }
    socket.setTransport(std::move(tlsTransport));
    LogI("[co_acquire_connection] TLS handshake ok: host={} elapsedMs={}", host,
         (tlsEnd - tlsStart).toMilliSeconds().count());
#endif
    co_return true;
}

}  // namespace

Task<std::unique_ptr<PooledConnection>> co_acquire_connection(
    std::string host,
    uint16_t port,
    std::string scheme,
    IPVersion ipVersion,
    std::chrono::milliseconds connectTimeout,
    std::chrono::milliseconds tlsHandshakeTimeout,
    RequestPerf* perf,
    CancellationToken token) {
    if (perf) {
        perf->host = host;
    }

    auto socket = std::make_unique<AsyncSocket>(ipVersion);
    if (!socket) {
        co_return nullptr;
    }

    std::chrono::milliseconds connectMs{0};
    if (!co_await resolveAndConnect(*socket, host, port, ipVersion, connectTimeout, connectMs, perf,
                                    token)) {
        co_return nullptr;
    }

#if ENABLE_HTTPS
    if (scheme == "https") {
        if (!co_await performTlsHandshake(*socket, host, tlsHandshakeTimeout, perf, token)) {
            co_return nullptr;
        }
    }
#endif

    if (perf) {
        perf->connection.connectResult = slark::Error::Success;
    }

    LogI("[co_acquire_connection] acquired: host={} port={} scheme={} connectMs={}", host, port,
         scheme, connectMs.count());

    co_return std::make_unique<PooledConnection>(std::move(socket));
}

}  // namespace slark::http
