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

Task<std::unique_ptr<AsyncSocket>> resolveAndConnect(
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
        co_return nullptr;
    }
    if (!addrInfo) {
        LogW("[co_acquire_connection] DNS resolution failed for {}", host);
        if (perf) {
            perf->connection.connectResult = slark::Error::DnsResolveFailed;
        }
        co_return nullptr;
    }

    const auto timeout = connectTimeout.count() > 0 ? connectTimeout : kDefaultConnectTimeout;
    Time::Deadline deadline(timeout);
    bool timedOut = false;
    bool failed = false;

    for (const auto* candidate = addrInfo.get(); candidate; candidate = candidate->ai_next) {
        if (token.isCancelled()) {
            co_return nullptr;
        }
        const auto remaining = deadline.remaining();
        if (remaining <= std::chrono::milliseconds(0)) {
            timedOut = true;
            break;
        }

        auto candidateAddress = makeConnectAddress(candidate);
        if (!candidateAddress) {
            failed = true;
            LogW("[co_acquire_connection] Invalid DNS address candidate for {}", host);
            continue;
        }

        auto socket = std::make_unique<AsyncSocket>(ipVersion);
        socket->setPerf(perf);
        const auto connectStart = Time::nowTimeStamp();
        try {
            co_await socket->asyncConnect(std::move(*candidateAddress), remaining, token);
        } catch (const CancelledException&) {
            LogW("[co_acquire_connection] Connect cancelled for {}", host);
            co_return nullptr;
        } catch (const TimeoutException&) {
            LogW("[co_acquire_connection] Connect timeout for {}", host);
            timedOut = true;
            break;
        } catch (const std::exception& e) {
            failed = true;
            LogW("[co_acquire_connection] Address candidate failed: {} {}", host, e.what());
            continue;
        }

        if (token.isCancelled()) {
            co_return nullptr;
        }
        connectMs = (Time::nowTimeStamp() - connectStart).toMilliSeconds();
        if (perf) {
            perf->connection.connectTimeMs = connectMs.count();
            perf->connection.connectResult = slark::Error::Success;
        }
        LogI("[co_acquire_connection] connected: host={} port={} remainingMs={}", host, port,
             deadline.remaining().count());
        co_return socket;
    }

    if (perf) {
        if (timedOut || deadline.isExpired()) {
            perf->transport.timeoutSource = TimeoutSource::Connect;
            perf->connection.connectResult = slark::Error::ConnectionTimeout;
        } else {
            perf->connection.connectResult = slark::Error::ConnectionFailed;
        }
    }
    if (failed && !timedOut) {
        DnsResolver::instance().invalidate(host);
    }
    co_return nullptr;
}

Task<bool> performTlsHandshake(AsyncSocket& socket,
                               const std::string& host,
                               std::chrono::milliseconds tlsHandshakeTimeout,
                               RequestPerf* perf,
                               CancellationToken& token) {
#if ENABLE_HTTPS
    auto tlsTransport = std::make_unique<TLSTransport>(socket.getFd(), host);
    auto tlsTimeout = tlsHandshakeTimeout.count() > 0 ? tlsHandshakeTimeout : kDefaultTlsTimeout;
    auto tlsStart = Time::nowTimeStamp();
    auto tlsResult = co_await tlsTransport->performHandshake(tlsTimeout, token);
    auto tlsEnd = Time::nowTimeStamp();

    if (perf) {
        perf->connection.tlsHandshakeTimeMs = (tlsEnd - tlsStart).toMilliSeconds().count();
    }
    if (tlsResult == slark::Error::Cancelled || token.isCancelled()) {
        co_return false;
    }
    if (tlsResult != slark::Error::Success) {
        LogW("[co_acquire_connection] TLS handshake failed: host={} error={}", host,
             static_cast<int>(tlsResult));
        if (perf) {
            perf->connection.connectResult = tlsResult;
            if (tlsResult == slark::Error::ConnectionTimeout) {
                perf->transport.timeoutSource = TimeoutSource::TlsHandshake;
            }
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

    std::chrono::milliseconds connectMs{0};
    auto socket = co_await resolveAndConnect(host, port, ipVersion, connectTimeout, connectMs, perf,
                                             token);
    if (!socket) {
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
