//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "AsyncSocket.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include <optional>
#include <utility>

#include "Log.hpp"
#include "SSLManager.h"
#include "ScopeGuard.h"
#include "Time.hpp"

#if ENABLE_HTTPS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace slark::http {

namespace {

Socket createAndConfigureSocket(int32_t addressFamily) {
    Socket sock = ::socket(addressFamily, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) {
        return kInvalidSocket;
    }

    ScopeGuard guard([sock] {
        ::close(sock);
    });

    int32_t flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        return kInvalidSocket;
    }

    int32_t optval = 1;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));

#if defined(TCP_KEEPIDLE)
    // Start keepalive probing after 60s of idle, adapting to mobile NAT timeouts
    int32_t keepIdle = 60;
    int32_t keepIntvl = 10;
    int32_t keepCnt = 3;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(keepIdle));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepIntvl, sizeof(keepIntvl));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCnt, sizeof(keepCnt));
#endif

#if defined(__APPLE__)
    setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &optval, sizeof(optval));
#endif

    guard.dismiss();
    return sock;
}

Task<void> tcpConnect(Socket socket, ConnectAddress address, std::chrono::milliseconds timeout) {
    auto connector = AsyncConnect(socket, std::move(address));
    connector.setTimeout(timeout);
    // A cancelled connect throws CancelledException from the awaiter itself.
    auto result = co_await connector;
    if (!result) {
        throw NetworkException(result.error(), "Connection failed");
    }
}

Task<bool> waitFdReadyWithTimeout(Socket socket,
                                  FdReadyAwaiter::Interest interest,
                                  Time::Deadline& deadline) {
    if (deadline.isExpired()) {
        co_return false;
    }

    const auto remaining = deadline.remaining();
    if (remaining <= std::chrono::milliseconds(0)) {
        co_return false;
    }

    bool ready = false;
    try {
        ready = co_await FdReadyAwaiter(socket, interest, remaining);
    } catch (const CancelledException&) {
        // The caller tells cancellation from a deadline expiry by checking its own token.
        co_return false;
    }
    if (!ready) {
        co_return false;
    }

    if (co_await coro::cancellation_requested()) {
        co_return false;
    }
    co_return !deadline.isExpired();
}

struct IoWait final {
    Time::Deadline& deadline;
    TimeoutSource timeoutSource;
    RequestPerf* perf;
};

Task<std::optional<Error>> checkIoWait(const IoWait& wait) {
    if (co_await coro::cancellation_requested()) {
        co_return Error::Cancelled;
    }
    if (wait.deadline.isExpired()) {
        if (wait.perf) {
            wait.perf->transport.timeoutSource = wait.timeoutSource;
        }
        co_return Error::ConnectionTimeout;
    }
    co_return std::nullopt;
}

Task<std::optional<Error>> waitForRetry(Socket socket,
                                        FdReadyAwaiter::Interest interest,
                                        const IoWait& wait) {
    if (co_await waitFdReadyWithTimeout(socket, interest, wait.deadline)) {
        co_return std::nullopt;
    }
    if (co_await coro::cancellation_requested()) {
        co_return Error::Cancelled;
    }
    if (wait.perf) {
        wait.perf->transport.timeoutSource = wait.timeoutSource;
    }
    co_return Error::ConnectionTimeout;
}

void closeSocket(Socket& socket) noexcept {
    if (socket != kInvalidSocket) {
        IOContext::instance().unregisterSocket(socket);
        ::close(socket);
        socket = kInvalidSocket;
    }
}

}  // anonymous namespace

AsyncSocket::AsyncSocket(IPVersion ipVersion, std::unique_ptr<ITransport> transport)
    : ipVersion_(ipVersion), socket_(kInvalidSocket), transport_(std::move(transport)) {}

AsyncSocket::~AsyncSocket() {
    close();
}

Task<void> AsyncSocket::asyncConnect(ConnectAddress address, std::chrono::milliseconds timeout) {
    if (!address.isValid()) {
        throw std::invalid_argument("Invalid connect address");
    }

    socket_ = createAndConfigureSocket(address.family);
    if (socket_ == kInvalidSocket) {
        LogE("[AsyncSocket::asyncConnect] create socket failed: family={}", address.family);
        throw std::runtime_error("Failed to create socket");
    }

    IOContext::instance().registerSocket(socket_);
    ScopeGuard guard([this] {
        closeSocket(socket_);
    });
    co_await tcpConnect(socket_, std::move(address), timeout);
    guard.dismiss();

    LogI("[AsyncSocket::asyncConnect] tcp connect ok: fd={} timeoutMs={}", socket_,
         timeout.count());

    if (!transport_) {
        transport_ = std::make_unique<PlainTransport>(socket_);
    }
}

Task<AsyncSocketResult> AsyncSocket::asyncSend(const std::string_view& data,
                                               std::chrono::milliseconds timeout) {
    if (socket_ == kInvalidSocket || !transport_) {
        co_return std::unexpected(Error::InvalidSocket);
    }

    Time::Deadline deadline(timeout);
    size_t totalSent = 0;
    int32_t retryCount = 0;
    const auto started = Time::nowTimeStamp();
    ScopeGuard sendTiming([this, started, &retryCount] {
        if (perf_) {
            perf_->transport.totalSendTimeMs +=
                (Time::nowTimeStamp() - started).toMilliSeconds().count();
            perf_->transport.sendRetryCount += retryCount;
        }
    });

    const IoWait wait{deadline, TimeoutSource::Write, perf_};
    while (totalSent < data.size()) {
        if (auto failure = co_await checkIoWait(wait)) {
            co_return std::unexpected(*failure);
        }

        ssize_t result = transport_->write(data.data() + totalSent, data.size() - totalSent);
        if (result > 0) {
            totalSent += static_cast<size_t>(result);
            continue;
        }

        if (transport_->needsRetry(result)) {
            ++retryCount;
            if (transport_->retryImmediately(result)) {
                continue;
            }
            if (auto failure = co_await waitForRetry(socket_, transport_->retryInterest(result),
                                                     wait)) {
                LogW("[AsyncSocket::asyncSend] write timeout: sent={}/{}", totalSent, data.size());
                co_return std::unexpected(*failure);
            }
            continue;
        }

        const auto error = transport_->classifyError(result);
        LogE("[AsyncSocket::asyncSend] write error: sent={}/{} error={} errno={}", totalSent,
             data.size(), toString(error), errno);
        co_return std::unexpected(error);
    }

    co_return totalSent;
}

Task<AsyncSocketResult> AsyncSocket::asyncReceive(void* buffer,
                                                  size_t size,
                                                  std::chrono::milliseconds timeout) {
    if (socket_ == kInvalidSocket || !transport_) {
        co_return std::unexpected(Error::InvalidSocket);
    }

    Time::Deadline deadline(timeout);
    int32_t waitCount = 0;
    const auto started = Time::nowTimeStamp();
    ScopeGuard receiveTiming([this, started, &waitCount] {
        if (perf_) {
            perf_->transport.totalReceiveTimeMs +=
                (Time::nowTimeStamp() - started).toMilliSeconds().count();
            perf_->transport.receiveWaitCount += waitCount;
        }
    });

    const IoWait wait{deadline, TimeoutSource::ReadIdle, perf_};
    while (true) {
        if (auto failure = co_await checkIoWait(wait)) {
            co_return std::unexpected(*failure);
        }

        ssize_t result = transport_->read(buffer, size);
        if (result > 0) {
            co_return static_cast<size_t>(result);
        }

        if (transport_->needsRetry(result)) {
            ++waitCount;
            if (transport_->retryImmediately(result)) {
                continue;
            }
            if (auto failure = co_await waitForRetry(socket_, transport_->retryInterest(result),
                                                     wait)) {
                LogW("[AsyncSocket::asyncReceive] read timeout");
                co_return std::unexpected(*failure);
            }
            continue;
        }

        if (result == 0) {
            if (transport_->isCleanEof(result)) {
                LogI("[AsyncSocket::asyncReceive] peer closed (EOF)");
                co_return size_t{0};
            }
            const auto error = transport_->classifyError(result);
            LogE("[AsyncSocket::asyncReceive] peer closed with transport error={}",
                 toString(error));
            co_return std::unexpected(error);
        }

        const auto error = transport_->classifyError(result);
        LogE("[AsyncSocket::asyncReceive] read error: error={} errno={}",
             toString(error), errno);
        co_return std::unexpected(error);
    }
}

void AsyncSocket::close() noexcept {
    if (transport_) {
        transport_->shutdown();
        transport_.reset();
    }
    closeSocket(socket_);
}

#if ENABLE_HTTPS

TLSTransport::TLSTransport(Socket fd, const std::string& hostname)
    : fd_(fd), sslPtr_(SSLManager::create(fd, hostname)) {}

TLSTransport::~TLSTransport() {
    shutdown();
}

Task<Error> TLSTransport::performHandshake(std::chrono::milliseconds timeout) {
    if (!sslPtr_) {
        LogW("TLS handshake failed: SSL create failed");
        co_return Error::HttpSslError;
    }

    Time::Deadline deadline(timeout);
    const IoWait wait{deadline, TimeoutSource::ReadIdle, nullptr};
    while (true) {
        if (auto failure = co_await checkIoWait(wait)) {
            if (*failure == Error::ConnectionTimeout) {
                LogW("TLS handshake timeout");
            }
            co_return *failure;
        }

        errno = 0;
        int32_t result = SSL_connect(sslPtr_.get());
        const int32_t savedErrno = errno;
        if (result == 1) {
            LogI("[TLSTransport::performHandshake] TLS handshake ok");
            co_return Error::Success;
        }

        const int32_t sslError = SSL_get_error(sslPtr_.get(), result);
        if (sslError == SSL_ERROR_SYSCALL && savedErrno == EINTR) {
            continue;
        }

        FdReadyAwaiter::Interest interest = FdReadyAwaiter::Interest::Read;
        if (sslError == SSL_ERROR_WANT_READ || sslError == SSL_ERROR_WANT_WRITE) {
            interest = (sslError == SSL_ERROR_WANT_READ) ? FdReadyAwaiter::Interest::Read
                                                         : FdReadyAwaiter::Interest::Write;
        } else if (sslError == SSL_ERROR_SYSCALL &&
                   (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK)) {
            interest = SSL_want_write(sslPtr_.get()) ? FdReadyAwaiter::Interest::Write
                                                     : FdReadyAwaiter::Interest::Read;
        } else {
            const char* errStr = ERR_reason_error_string(ERR_get_error());
            LogW("TLS handshake failed: sslError={}, errStr={}, errno={}", sslError,
                 errStr ? errStr : "unknown", savedErrno);
            co_return Error::HttpSslHandshakeFailed;
        }

        lastInterest_ = interest;
        if (auto failure = co_await waitForRetry(fd_, interest, wait)) {
            LogW("TLS handshake failed: waitFdReady timeout");
            co_return *failure;
        }
    }
}

ssize_t TLSTransport::write(const void* data, size_t size) noexcept {
    lastInterest_ = FdReadyAwaiter::Interest::Write;
    errno = 0;
    auto writeSize = static_cast<int32_t>(std::min(size, static_cast<size_t>(INT32_MAX)));
    int32_t result = SSL_write(sslPtr_.get(), data, writeSize);
    lastErrno_ = errno;
    if (result <= 0) {
        lastSslError_ = SSL_get_error(sslPtr_.get(), result);
    }
    return result;
}

ssize_t TLSTransport::read(void* buffer, size_t size) noexcept {
    lastInterest_ = FdReadyAwaiter::Interest::Read;
    errno = 0;
    auto readSize = static_cast<int32_t>(std::min(size, static_cast<size_t>(INT32_MAX)));
    int32_t result = SSL_read(sslPtr_.get(), buffer, readSize);
    lastErrno_ = errno;
    if (result <= 0) {
        lastSslError_ = SSL_get_error(sslPtr_.get(), result);
    }
    return result;
}

bool TLSTransport::needsRetry(ssize_t result) const noexcept {
    if (lastSslError_ == SSL_ERROR_WANT_READ || lastSslError_ == SSL_ERROR_WANT_WRITE) {
        return true;
    }
    // Some Android TLS stacks report SSL_ERROR_SSL while the nonblocking syscall
    // only means that more data is needed. The errno is authoritative here.
    return result <= 0 && isTransientSocketError(lastErrno_);
}

bool TLSTransport::retryImmediately(ssize_t) const noexcept {
    return lastErrno_ == EINTR;
}

bool TLSTransport::isCleanEof(ssize_t result) const noexcept {
    return result == 0 && lastSslError_ == SSL_ERROR_ZERO_RETURN;
}

Error TLSTransport::classifyError(ssize_t result) const noexcept {
    if (lastSslError_ == SSL_ERROR_SSL) {
        return Error::HttpSslError;
    }
    if (lastSslError_ == SSL_ERROR_ZERO_RETURN) {
        return Error::SocketClosed;
    }
    if (lastSslError_ == SSL_ERROR_SYSCALL) {
        return result == 0 ? Error::ConnectionReset : mapTransportErrno(lastErrno_);
    }
    return Error::SocketError;
}

FdReadyAwaiter::Interest TLSTransport::retryInterest(ssize_t) const noexcept {
    if (lastSslError_ == SSL_ERROR_WANT_READ) {
        return FdReadyAwaiter::Interest::Read;
    }
    if (lastSslError_ == SSL_ERROR_WANT_WRITE) {
        return FdReadyAwaiter::Interest::Write;
    }
    return lastInterest_;
}

void TLSTransport::shutdown() noexcept {
    if (sslPtr_) {
        int32_t ret = SSL_shutdown(sslPtr_.get());
        if (ret == 0) {
            // close_notify already sent; wait for the peer's reply (non-blocking, ignore errors)
            SSL_shutdown(sslPtr_.get());
        }
        sslPtr_.reset();
    }
}

#endif

}  // namespace slark::http