//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "ConnectionPool.h"

#include "DnsResolver.h"
#include "IOContext.h"
#include "Log.hpp"

namespace slark::http {

static constexpr std::chrono::milliseconds kDefaultTlsTimeout{std::chrono::seconds{10}};

Task<std::unique_ptr<PooledConnection>> co_acquire_connection(
    std::string host,
    uint16_t port,
    std::string scheme,
    IPVersion ipVersion,
    std::chrono::milliseconds connectTimeout,
    std::chrono::milliseconds tlsHandshakeTimeout,
    RequestPerf* perf) {
    if (perf) {
        perf->host = host;
    }

    auto socket = std::make_unique<AsyncSocket>(ipVersion);
    if (!socket) {
        co_return nullptr;
    }

    // DNS 解析
    auto addrInfo =
        co_await DnsResolver::instance().resolveAsync(host, std::to_string(port), ipVersion);
    if (!addrInfo) {
        LogW("[co_acquire_connection] DNS resolution failed for {}", host);
        co_return nullptr;
    }

    // TCP 连接
    auto ct = connectTimeout.count() > 0 ? connectTimeout : kDefaultConnectTimeout;
    auto connectStart = Time::nowTimeStamp();
    try {
        socket->setPerf(perf);
        co_await socket->asyncConnect(addrInfo, ct);
    } catch (const TimeoutException&) {
        LogW("[co_acquire_connection] Connect timeout for {}", host);
        if (perf) {
            perf->transport.timeoutSource = TimeoutSource::Connect;
        }
        co_return nullptr;
    } catch (const std::exception& e) {
        LogW("[co_acquire_connection] Connect failed: {} {}", host, e.what());
        DnsResolver::instance().invalidate(host);
        co_return nullptr;
    }
    auto connectEnd = Time::nowTimeStamp();

    if (perf) {
        perf->connection.connectTimeMs = (connectEnd - connectStart).toMilliSeconds().count();
    }

    LogI("[co_acquire_connection] connected: host={} port={}", host, port);

    // TLS 握手
#if ENABLE_HTTPS
    if (scheme == "https") {
        auto tlsTransport = std::make_unique<TLSTransport>(socket->getFd(), host);
        auto tlsTimeout =
            tlsHandshakeTimeout.count() > 0 ? tlsHandshakeTimeout : kDefaultTlsTimeout;
        auto tlsStart = Time::nowTimeStamp();
        bool tlsOk = co_await tlsTransport->performHandshake(tlsTimeout);
        auto tlsEnd = Time::nowTimeStamp();

        if (perf) {
            perf->connection.tlsHandshakeTimeMs = (tlsEnd - tlsStart).toMilliSeconds().count();
        }

        if (!tlsOk) {
            LogW("[co_acquire_connection] TLS handshake failed: host={}", host);
            if (perf) {
                perf->transport.timeoutSource = TimeoutSource::TlsHandshake;
            }
            co_return nullptr;
        }
        socket->setTransport(std::move(tlsTransport));

        LogI("[co_acquire_connection] TLS handshake ok: host={} elapsedMs={}", host,
             (tlsEnd - tlsStart).toMilliSeconds().count());
    }
#endif

    if (perf) {
        perf->connection.connectResult = slark::Error::Success;
    }

    LogI("[co_acquire_connection] acquired: host={} port={} scheme={} connectMs={}", host, port,
         scheme, (connectEnd - connectStart).toMilliSeconds().count());

    co_return std::make_unique<PooledConnection>(std::move(socket));
}

}  // namespace slark::http
