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

Task<std::expected<std::unique_ptr<AsyncSocket>, ErrorInfo>> resolveAndConnect(
    const std::string& host,
    uint16_t port,
    IPVersion ipVersion,
    NetworkAddressPolicy addressPolicy,
    std::chrono::milliseconds connectTimeout,
    std::chrono::milliseconds& connectMs,
    RequestPerf* perf,
    CancellationToken& token) {
    auto addrInfo =
        co_await DnsResolver::instance().resolveAsync(host, std::to_string(port), ipVersion, token);
    if (token.isCancelled()) {
        co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
    }
    if (!addrInfo) {
        LogW("[co_acquire_connection] DNS resolution failed for {}", host);
        if (perf) {
            perf->connection.connectResult = slark::Error::DnsResolveFailed;
        }
        co_return std::unexpected(ErrorInfo{slark::Error::DnsResolveFailed});
    }

    const auto timeout = connectTimeout.count() > 0 ? connectTimeout : kDefaultConnectTimeout;
    Time::Deadline deadline(timeout);
    bool policyRejected = false;
    bool attemptedConnection = false;
    ErrorInfo lastError{slark::Error::ConnectionFailed};

    for (const auto* candidate = addrInfo.get(); candidate; candidate = candidate->ai_next) {
        if (token.isCancelled()) {
            co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
        }

        const auto remaining = deadline.remaining();
        if (remaining <= std::chrono::milliseconds(0)) {
            if (perf) {
                perf->transport.timeoutSource = TimeoutSource::Connect;
                perf->connection.connectResult = slark::Error::ConnectionTimeout;
            }
            co_return std::unexpected(ErrorInfo{slark::Error::ConnectionTimeout});
        }

        auto candidateAddress = makeConnectAddress(candidate);
        if (!candidateAddress) {
            LogW("[co_acquire_connection] invalid address candidate for {}", host);
            continue;
        }
        if (addressPolicy == NetworkAddressPolicy::PublicOnly &&
            !isPublicConnectAddress(*candidateAddress)) {
            policyRejected = true;
            LogW("[co_acquire_connection] rejected non-public address for {}", host);
            continue;
        }

        attemptedConnection = true;
        auto socket = std::make_unique<AsyncSocket>(ipVersion);
        socket->setPerf(perf);
        const auto connectStart = Time::nowTimeStamp();
        try {
            co_await socket->asyncConnect(std::move(*candidateAddress), remaining, token);
        } catch (const CancelledException&) {
            LogW("[co_acquire_connection] connect cancelled for {}", host);
            co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
        } catch (const TimeoutException&) {
            LogW("[co_acquire_connection] connect timeout for {}", host);
            if (perf) {
                perf->transport.timeoutSource = TimeoutSource::Connect;
                perf->connection.connectResult = slark::Error::ConnectionTimeout;
            }
            co_return std::unexpected(ErrorInfo{slark::Error::ConnectionTimeout});
        } catch (const std::exception& e) {
            lastError = ErrorInfo{slark::Error::ConnectionFailed};
            LogW("[co_acquire_connection] address candidate failed: {} {}", host, e.what());
            continue;
        }

        if (token.isCancelled()) {
            co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
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

    if (policyRejected && !attemptedConnection) {
        lastError = ErrorInfo{slark::Error::HttpRedirectError};
    }
    if (perf) {
        perf->connection.connectResult = lastError.retCode;
    }
    if (attemptedConnection && lastError.retCode == slark::Error::ConnectionFailed) {
        DnsResolver::instance().invalidate(host);
    }
    co_return std::unexpected(lastError);
}

Task<Error> performTlsHandshake(AsyncSocket& socket,
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
        co_return slark::Error::Cancelled;
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
        co_return tlsResult;
    }
    socket.setTransport(std::move(tlsTransport));
    LogI("[co_acquire_connection] TLS handshake ok: host={} elapsedMs={}", host,
         (tlsEnd - tlsStart).toMilliSeconds().count());
#endif
    co_return slark::Error::Success;
}

}  // namespace

Task<ConnectionResult> co_acquire_connection(
    std::string host,
    uint16_t port,
    std::string scheme,
    IPVersion ipVersion,
    NetworkAddressPolicy addressPolicy,
    std::chrono::milliseconds connectTimeout,
    std::chrono::milliseconds tlsHandshakeTimeout,
    RequestPerf* perf,
    CancellationToken token) {
    if (perf) {
        perf->host = host;
    }

    std::chrono::milliseconds connectMs{0};
    auto socketResult = co_await resolveAndConnect(host, port, ipVersion, addressPolicy,
                                                    connectTimeout, connectMs, perf, token);
    if (!socketResult) {
        co_return std::unexpected(socketResult.error());
    }
    auto socket = std::move(*socketResult);

#if ENABLE_HTTPS
    if (scheme == "https") {
        const auto tlsError = co_await performTlsHandshake(*socket, host, tlsHandshakeTimeout, perf,
                                                           token);
        if (tlsError != slark::Error::Success) {
            if (perf) {
                perf->connection.connectResult = tlsError;
                if (tlsError == slark::Error::ConnectionTimeout) {
                    perf->transport.timeoutSource = TimeoutSource::TlsHandshake;
                }
            }
            co_return std::unexpected(ErrorInfo{tlsError});
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
