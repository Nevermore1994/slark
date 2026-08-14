//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "AsyncSocket.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <unistd.h>

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

Task<void> tcpConnect(Socket socket,
                      ConnectAddress address,
                      std::chrono::milliseconds timeout,
                      CancellationToken token = CancellationToken{}) {
    auto connector = AsyncConnect(socket, std::move(address));
    connector.setTimeout(timeout);
    connector.setCancellationToken(token);
    auto result = co_await connector;
    if (!result) {
        auto error = result.error();
        if (error == Error::Cancelled) {
            throw CancelledException("Connection cancelled");
        }
        throw NetworkException(error, "Connection failed");
    }
}

Task<bool> waitFdReadyWithTimeout(Socket socket,
                                  FdReadyAwaiter::Interest interest,
                                  Time::Deadline& deadline,
                                  CancellationToken* externalToken = nullptr) {
    if (deadline.isExpired()) {
        co_return false;
    }

    auto remaining = deadline.remaining();
    if (remaining <= std::chrono::milliseconds(0)) {
        co_return false;
    }

    CancellationToken localToken;
    auto timerId =
        CoroutineScheduler::instance().scheduleCallback(remaining, [token = localToken]() mutable {
            token.cancel();
        });

    ScopeGuard timerGuard([timerId]() {
        CoroutineScheduler::instance().cancel(timerId);
    });

    // registration must outlive the entire wait, otherwise the callback gets unregistered
    CancellationTokenRegistration registration;
    if (externalToken) {
        registration = externalToken->registerCallback([token = localToken]() mutable {
            token.cancel();
        });
        if (!registration.isValid()) {
            co_return false;
        }
    }

    auto fdInterest = interest;

    auto awaiter = withCancellation(FdReadyAwaiter(socket, fdInterest), localToken);
    bool ready = co_await awaiter;
    if (!ready) {
        co_return false;
    }

    if (externalToken && externalToken->isCancelled()) {
        co_return false;
    }

    co_return !deadline.isExpired();
}

void closeSocket(Socket& socket) noexcept {
    if (socket != kInvalidSocket) {
        IOContext::instance().unregisterSocket(socket);
        ::close(socket);
        socket = kInvalidSocket;
    }
}

}  // anonymous namespace

// AsyncSocket

AsyncSocket::AsyncSocket(IPVersion ipVersion, std::unique_ptr<ITransport> transport)
    : ipVersion_(ipVersion), socket_(kInvalidSocket), transport_(std::move(transport)) {}

AsyncSocket::~AsyncSocket() {
    close();
}

Task<void> AsyncSocket::asyncConnect(ConnectAddress address,
                                     std::chrono::milliseconds timeout,
                                     CancellationToken token) {
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
    co_await tcpConnect(socket_, std::move(address), timeout, token);
    guard.dismiss();

    LogI("[AsyncSocket::asyncConnect] tcp connect ok: fd={} timeoutMs={}", socket_,
         timeout.count());

    if (!transport_) {
        transport_ = std::make_unique<PlainTransport>(socket_);
    }
}

Task<AsyncSocketResult> AsyncSocket::asyncSend(const std::string_view& data,
                                               std::chrono::milliseconds timeout) {
    return asyncSend(data, timeout, CancellationToken{});
}

Task<AsyncSocketResult> AsyncSocket::asyncSend(const std::string_view& data,
                                               std::chrono::milliseconds timeout,
                                               CancellationToken token) {
    if (socket_ == kInvalidSocket || !transport_) {
        co_return std::unexpected(Error::InvalidSocket);
    }

    Time::Deadline deadline(timeout);
    size_t totalSent = 0;
    int32_t retryCount = 0;

    while (totalSent < data.size()) {
        if (token.isCancelled()) {
            co_return std::unexpected(Error::Cancelled);
        }

        if (deadline.isExpired()) {
            if (perf_) {
                perf_->transport.timeoutSource = TimeoutSource::Write;
            }
            co_return std::unexpected(Error::ConnectionTimeout);
        }

        ssize_t result = transport_->write(data.data() + totalSent, data.size() - totalSent);
        if (result > 0) {
            totalSent += static_cast<size_t>(result);
            continue;
        }

        if (transport_->needsRetry(result)) {
            retryCount++;
            if (transport_->retryImmediately(result)) {
                continue;
            }
            bool ready = co_await waitFdReadyWithTimeout(socket_, transport_->retryInterest(result),
                                                         deadline, &token);
            if (!ready) {
                if (token.isCancelled()) {
                    co_return std::unexpected(Error::Cancelled);
                }
                if (perf_) {
                    perf_->transport.timeoutSource = TimeoutSource::Write;
                }
                LogW("[AsyncSocket::asyncSend] write timeout: sent={}/{}", totalSent, data.size());
                co_return std::unexpected(Error::ConnectionTimeout);
            }
            continue;
        }

        const auto error = transport_->classifyError(result);
        LogE("[AsyncSocket::asyncSend] write error: sent={}/{} error={} errno={}", totalSent,
             data.size(), static_cast<int>(error), errno);
        co_return std::unexpected(error);
    }

    if (perf_) {
        perf_->transport.sendRetryCount += retryCount;
    }
    co_return totalSent;
}

Task<AsyncSocketResult> AsyncSocket::asyncReceive(void* buffer,
                                                  size_t size,
                                                  std::chrono::milliseconds timeout) {
    return asyncReceive(buffer, size, timeout, CancellationToken{});
}

Task<AsyncSocketResult> AsyncSocket::asyncReceive(void* buffer,
                                                  size_t size,
                                                  std::chrono::milliseconds timeout,
                                                  CancellationToken token) {
    if (socket_ == kInvalidSocket || !transport_) {
        co_return std::unexpected(Error::InvalidSocket);
    }

    Time::Deadline deadline(timeout);
    int32_t waitCount = 0;

    while (true) {
        if (token.isCancelled()) {
            co_return std::unexpected(Error::Cancelled);
        }

        if (deadline.isExpired()) {
            if (perf_) {
                perf_->transport.timeoutSource = TimeoutSource::ReadIdle;
            }
            co_return std::unexpected(Error::ConnectionTimeout);
        }

        ssize_t result = transport_->read(buffer, size);
        if (result > 0) {
            if (perf_) {
                perf_->transport.receiveWaitCount += waitCount;
            }
            co_return static_cast<size_t>(result);
        }

        if (transport_->needsRetry(result)) {
            waitCount++;
            if (transport_->retryImmediately(result)) {
                continue;
            }
            bool ready = co_await waitFdReadyWithTimeout(socket_, transport_->retryInterest(result),
                                                         deadline, &token);
            if (!ready) {
                if (token.isCancelled()) {
                    co_return std::unexpected(Error::Cancelled);
                }
                if (perf_) {
                    perf_->transport.timeoutSource = TimeoutSource::ReadIdle;
                }
                LogW("[AsyncSocket::asyncReceive] read timeout");
                co_return std::unexpected(Error::ConnectionTimeout);
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
                 static_cast<int>(error));
            co_return std::unexpected(error);
        }

        const auto error = transport_->classifyError(result);
        LogE("[AsyncSocket::asyncReceive] read error: error={} errno={}",
             static_cast<int>(error), errno);
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

// TLSTransport

#if ENABLE_HTTPS

TLSTransport::TLSTransport(Socket fd, const std::string& hostname)
    : fd_(fd), sslPtr_(SSLManager::create(fd, hostname)) {}

TLSTransport::~TLSTransport() {
    shutdown();
}

Task<Error> TLSTransport::performHandshake(std::chrono::milliseconds timeout,
                                           CancellationToken token) {
    if (!sslPtr_) {
        LogW("TLS handshake failed: SSL create failed");
        co_return Error::HttpSslError;
    }

    Time::Deadline deadline(timeout);
    while (true) {
        if (token.isCancelled()) {
            co_return Error::Cancelled;
        }
        if (deadline.isExpired()) {
            LogW("TLS handshake timeout");
            co_return Error::ConnectionTimeout;
        }

        errno = 0;
        int32_t result = SSL_connect(sslPtr_.get());
        const int32_t savedErrno = errno;
        if (result == 1) {
            LogI("[TLSTransport::performHandshake] TLS handshake ok");
            co_return Error::Success;
        }

        int32_t sslError = SSL_get_error(sslPtr_.get(), result);
        if (sslError == SSL_ERROR_WANT_READ || sslError == SSL_ERROR_WANT_WRITE) {
            lastInterest_ = (sslError == SSL_ERROR_WANT_READ) ? FdReadyAwaiter::Interest::Read
                                                              : FdReadyAwaiter::Interest::Write;
            bool ready = co_await waitFdReadyWithTimeout(fd_, lastInterest_, deadline, &token);
            if (!ready) {
                LogW("TLS handshake failed: waitFdReady timeout");
                co_return token.isCancelled() ? Error::Cancelled : Error::ConnectionTimeout;
            }
        } else if (sslError == SSL_ERROR_SYSCALL && savedErrno == EINTR) {
            continue;
        } else if (sslError == SSL_ERROR_SYSCALL &&
                   (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK)) {
            const auto interest = SSL_want_write(sslPtr_.get())
                                      ? FdReadyAwaiter::Interest::Write
                                      : FdReadyAwaiter::Interest::Read;
            lastInterest_ = interest;
            bool ready = co_await waitFdReadyWithTimeout(fd_, interest, deadline, &token);
            if (!ready) {
                LogW("TLS handshake failed: waitFdReady timeout");
                co_return token.isCancelled() ? Error::Cancelled : Error::ConnectionTimeout;
            }
        } else {
            const char* errStr = ERR_reason_error_string(ERR_get_error());
            LogW("TLS handshake failed: sslError={}, errStr={}, errno={}", sslError,
                 errStr ? errStr : "unknown", savedErrno);
            co_return Error::HttpSslHandshakeFailed;
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