//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "ConnectionFactory.h"

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
    RequestPerf* perf) {
    AddressInfoPtr addrInfo;
    try {
        addrInfo = co_await DnsResolver::instance().resolveAsync(host, std::to_string(port),
                                                                 ipVersion);
    } catch (const CancelledException&) {
        co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
    }
    if (co_await coro::cancellation_requested()) {
        co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
    }
    if (!addrInfo) {
        LogW("[openConnection] DNS resolution failed for {}", host);
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

    size_t candidateCount = 0;
    for (const auto* candidate = addrInfo.get(); candidate; candidate = candidate->ai_next) {
        ++candidateCount;
    }

    for (const auto* candidate = addrInfo.get(); candidate; candidate = candidate->ai_next) {
        if (co_await coro::cancellation_requested()) {
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
            LogW("[openConnection] invalid address candidate for {}", host);
            --candidateCount;
            continue;
        }
        if (addressPolicy == NetworkAddressPolicy::PublicOnly &&
            !isPublicConnectAddress(*candidateAddress)) {
            policyRejected = true;
            LogW("[openConnection] rejected non-public address for {}", host);
            --candidateCount;
            continue;
        }

        attemptedConnection = true;
        const auto attemptBudget = connectAttemptBudget(remaining, candidateCount);
        --candidateCount;
        auto socket = std::make_unique<AsyncSocket>(ipVersion);
        socket->setPerf(perf);
        const auto connectStart = Time::nowTimeStamp();
        try {
            co_await socket->asyncConnect(std::move(*candidateAddress), attemptBudget);
        } catch (const CancelledException&) {
            LogW("[openConnection] connect cancelled for {}", host);
            co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
        } catch (const NetworkException& e) {
            lastError = ErrorInfo{e.error()};
            LogW("[openConnection] address candidate failed: {} error={}", host,
                 toString(e.error()));
            if (e.error() == slark::Error::ConnectionTimeout && perf) {
                perf->transport.timeoutSource = TimeoutSource::Connect;
                perf->connection.connectResult = e.error();
            }
            // A timed-out candidate must not starve the remaining addresses
            continue;
        } catch (const TimeoutException&) {
            LogW("[openConnection] connect timeout for {}", host);
            lastError = ErrorInfo{slark::Error::ConnectionTimeout};
            if (perf) {
                perf->transport.timeoutSource = TimeoutSource::Connect;
                perf->connection.connectResult = slark::Error::ConnectionTimeout;
            }
            continue;
        } catch (const std::exception& e) {
            lastError = ErrorInfo{slark::Error::ConnectionFailed};
            LogW("[openConnection] address candidate failed: {} {}", host, e.what());
            continue;
        }

        if (co_await coro::cancellation_requested()) {
            co_return std::unexpected(ErrorInfo{slark::Error::Cancelled});
        }
        connectMs = (Time::nowTimeStamp() - connectStart).toMilliSeconds();
        if (perf) {
            perf->connection.connectTimeMs = connectMs.count();
            perf->connection.connectResult = slark::Error::Success;
        }
        LogI("[openConnection] connected: host={} port={} remainingMs={}", host, port,
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
                                RequestPerf* perf) {
#if ENABLE_HTTPS
    auto tlsTransport = std::make_unique<TLSTransport>(socket.getFd(), host);
    auto tlsTimeout = tlsHandshakeTimeout.count() > 0 ? tlsHandshakeTimeout : kDefaultTlsTimeout;
    auto tlsStart = Time::nowTimeStamp();
    auto tlsResult = co_await tlsTransport->performHandshake(tlsTimeout);
    auto tlsEnd = Time::nowTimeStamp();

    if (perf) {
        perf->connection.tlsHandshakeTimeMs = (tlsEnd - tlsStart).toMilliSeconds().count();
    }
    if (tlsResult == slark::Error::Cancelled || (co_await coro::cancellation_requested())) {
        co_return slark::Error::Cancelled;
    }
    if (tlsResult != slark::Error::Success) {
        LogW("[openConnection] TLS handshake failed: host={} error={}", host,
             toString(tlsResult));
        if (perf) {
            perf->connection.connectResult = tlsResult;
            if (tlsResult == slark::Error::ConnectionTimeout) {
                perf->transport.timeoutSource = TimeoutSource::TlsHandshake;
            }
        }
        co_return tlsResult;
    }
    socket.setTransport(std::move(tlsTransport));
    LogI("[openConnection] TLS handshake ok: host={} elapsedMs={}", host,
         (tlsEnd - tlsStart).toMilliSeconds().count());
#endif
    co_return slark::Error::Success;
}

}  // namespace

Task<OpenConnectionResult> openConnection(std::string host,
                                          uint16_t port,
                                          std::string scheme,
                                          IPVersion ipVersion,
                                          NetworkAddressPolicy addressPolicy,
                                          std::chrono::milliseconds connectTimeout,
                                          std::chrono::milliseconds tlsHandshakeTimeout,
                                          RequestPerf* perf,
                                          CancellationToken token) {
    auto task = openConnection(std::move(host), port, std::move(scheme), ipVersion, addressPolicy,
                               connectTimeout, tlsHandshakeTimeout, perf);
    task.attach(SpawnEdge{}, &token);
    return task;
}

Task<OpenConnectionResult> openConnection(
    std::string host,
    uint16_t port,
    std::string scheme,
    IPVersion ipVersion,
    NetworkAddressPolicy addressPolicy,
    std::chrono::milliseconds connectTimeout,
    std::chrono::milliseconds tlsHandshakeTimeout,
    RequestPerf* perf) {
    if (perf) {
        perf->host = host;
    }

    std::chrono::milliseconds connectMs{0};
    auto socketResult = co_await resolveAndConnect(host, port, ipVersion, addressPolicy,
                                                   connectTimeout, connectMs, perf);
    if (!socketResult) {
        co_return std::unexpected(socketResult.error());
    }
    auto socket = std::move(*socketResult);

#if ENABLE_HTTPS
    if (scheme == "https") {
        const auto tlsError =
            co_await performTlsHandshake(*socket, host, tlsHandshakeTimeout, perf);
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

    LogI("[openConnection] acquired: host={} port={} scheme={} connectMs={}", host, port,
         scheme, connectMs.count());

    co_return std::make_unique<OwnedConnection>(std::move(socket));
}

}  // namespace slark::http
