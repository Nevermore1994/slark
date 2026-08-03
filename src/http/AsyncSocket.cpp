//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "AsyncSocket.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <unistd.h>

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

std::pair<std::string, uint16_t> extractAddress(const AddressInfoPtr& address) {
    char addrStr[INET6_ADDRSTRLEN] = {};
    uint16_t port = 0;

    if (address->ai_family == AF_INET) {
        auto* addr = reinterpret_cast<sockaddr_in*>(address->ai_addr);
        inet_ntop(AF_INET, &addr->sin_addr, addrStr, sizeof(addrStr));
        port = ntohs(addr->sin_port);
    } else if (address->ai_family == AF_INET6) {
        auto* addr = reinterpret_cast<sockaddr_in6*>(address->ai_addr);
        inet_ntop(AF_INET6, &addr->sin6_addr, addrStr, sizeof(addrStr));
        port = ntohs(addr->sin6_port);
    }

    return {addrStr, port};
}

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
    // 60s 无数据后开始发送 keepalive 探测，适配移动网络 NAT 超时
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
                      const AddressInfoPtr& address,
                      std::chrono::milliseconds timeout,
                      CancellationToken token = CancellationToken{}) {
    auto [addrStr, port] = extractAddress(address);
    auto connector = AsyncConnect(socket, addrStr, port);
    connector.setTimeout(timeout);
    connector.setCancellationToken(token);
    auto result = co_await connector;
    if (!result) {
        auto error = result.error();
        if (error == Error::Cancelled) {
            throw CancelledException("Connection cancelled");
        }
        if (error == Error::ConnectionTimeout) {
            throw TimeoutException("Connection timeout");
        }
        throw std::runtime_error("Connection failed");
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

    // registration 必须在整个等待期间存活，否则回调会被注销
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

Task<void> AsyncSocket::asyncConnect(const AddressInfoPtr& address,
                                     std::chrono::milliseconds timeout,
                                     CancellationToken token) {
    socket_ = createAndConfigureSocket(address->ai_family);
    if (socket_ == kInvalidSocket) {
        LogE("[AsyncSocket::asyncConnect] create socket failed: family={}",
             static_cast<int>(address->ai_family));
        throw std::runtime_error("Failed to create socket");
    }

    IOContext::instance().registerSocket(socket_);
    ScopeGuard guard([this] {
        closeSocket(socket_);
    });
    co_await tcpConnect(socket_, address, timeout, token);
    guard.dismiss();

    LogI("[AsyncSocket::asyncConnect] tcp connect ok: fd={} timeoutMs={}", socket_,
         timeout.count());

    // 如果没有预设 transport，默认使用 PlainTransport
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
        // 检查取消状态
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

        LogE("[AsyncSocket::asyncSend] broken pipe: sent={}/{} errno={}", totalSent, data.size(),
             errno);
        co_return std::unexpected(Error::BrokenPipe);
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
        // 检查取消状态
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

        if (result == 0) {
            LogW("[AsyncSocket::asyncReceive] peer closed (EOF)");
            co_return std::unexpected(Error::ConnectionReset);
        }

        if (transport_->needsRetry(result)) {
            waitCount++;
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

        LogE("[AsyncSocket::asyncReceive] read error: errno={}", errno);
        co_return std::unexpected(Error::ConnectionReset);
    }
}

Task<DataPtr> AsyncSocket::asyncReceiveAll(std::chrono::milliseconds timeout) {
    if (socket_ == kInvalidSocket || !transport_) {
        co_return nullptr;
    }

    Time::Deadline deadline(timeout);
    auto data = std::make_unique<Data>();
    data->resize(kDefaultReadSize);

    int32_t waitCount = 0;
    size_t totalReceived = 0;
    while (true) {
        if (deadline.isExpired()) {
            if (perf_) {
                perf_->transport.timeoutSource = TimeoutSource::ReadOverall;
            }
            break;
        }

        ssize_t result =
            transport_->read(data->data() + totalReceived, data->size() - totalReceived);
        if (result > 0) {
            totalReceived += static_cast<size_t>(result);
            if (data->size() - totalReceived < static_cast<size_t>(kDefaultReadSize)) {
                // 渐进式扩容：小于阈值时加倍，大于阈值时按固定步长增长
                auto currentSize = data->size();
                auto newSize = (currentSize < kGrowthThreshold) ? currentSize * 2
                                                                : currentSize + kGrowthStepSize;
                if (newSize > kMaxReceiveBufferSize) {
                    break;
                }
                data->resize(newSize);
            }
            continue;
        }

        if (result == 0) {
            break;
        }

        if (transport_->needsRetry(result)) {
            waitCount++;
            bool ready = co_await waitFdReadyWithTimeout(socket_, transport_->retryInterest(result),
                                                         deadline);
            if (!ready) {
                if (perf_) {
                    perf_->transport.timeoutSource = TimeoutSource::ReadOverall;
                }
                break;
            }
            continue;
        }

        break;
    }

    if (perf_) {
        perf_->transport.receiveWaitCount += waitCount;
    }
    data->resize(totalReceived);
    co_return std::move(data);
}

Task<bool> AsyncSocket::asyncReceiveStreaming(HttpResponseParser& parser,
                                              std::chrono::milliseconds timeout,
                                              CancellationToken token) {
    if (socket_ == kInvalidSocket || !transport_) {
        co_return false;
    }

    Time::Deadline deadline(timeout);
    std::array<Byte, kDefaultReadSize> readBuffer{};
    int32_t waitCount = 0;

    while (!parser.isCompleted() && !parser.hasError()) {
        // 检查取消状态，实现快速响应
        if (token.isCancelled()) {
            co_return false;
        }

        if (deadline.isExpired()) {
            if (perf_) {
                perf_->transport.timeoutSource = TimeoutSource::ReadOverall;
            }
            co_return false;
        }

        ssize_t result = transport_->read(readBuffer.data(), readBuffer.size());
        if (result > 0) {
            parser.feed(readBuffer.data(), static_cast<size_t>(result));
            continue;
        }

        if (result == 0) {
            // 连接关闭，通知 parser EOF 到达
            parser.onEof();
            break;
        }

        if (transport_->needsRetry(result)) {
            waitCount++;
            bool ready = co_await waitFdReadyWithTimeout(socket_, transport_->retryInterest(result),
                                                         deadline, &token);
            if (!ready) {
                if (perf_) {
                    perf_->transport.timeoutSource =
                        token.isCancelled() ? TimeoutSource::None : TimeoutSource::ReadIdle;
                }
                co_return false;
            }
            continue;
        }

        co_return false;
    }

    if (perf_) {
        perf_->transport.receiveWaitCount += waitCount;
    }
    co_return parser.isCompleted();
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

Task<bool> TLSTransport::performHandshake(std::chrono::milliseconds timeout,
                                          CancellationToken token) {
    if (!sslPtr_) {
        LogW("TLS handshake failed: SSL create failed");
        co_return false;
    }

    Time::Deadline deadline(timeout);
    while (true) {
        if (token.isCancelled()) {
            co_return false;
        }
        if (deadline.isExpired()) {
            LogW("TLS handshake timeout");
            co_return false;
        }

        int32_t result = SSL_connect(sslPtr_.get());
        if (result == 1) {
            LogI("[TLSTransport::performHandshake] TLS handshake ok");
            co_return true;
        }

        int32_t sslError = SSL_get_error(sslPtr_.get(), result);
        if (sslError == SSL_ERROR_WANT_READ || sslError == SSL_ERROR_WANT_WRITE) {
            auto interest = (sslError == SSL_ERROR_WANT_READ) ? FdReadyAwaiter::Interest::Read
                                                              : FdReadyAwaiter::Interest::Write;
            bool ready = co_await waitFdReadyWithTimeout(fd_, interest, deadline, &token);
            if (!ready) {
                LogW("TLS handshake failed: waitFdReady timeout");
                co_return false;
            }
        } else {
            const char* errStr = ERR_reason_error_string(ERR_get_error());
            LogW("TLS handshake failed: sslError={}, errStr={}, errno={}", sslError,
                 errStr ? errStr : "unknown", errno);
            co_return false;
        }
    }
}

ssize_t TLSTransport::write(const void* data, size_t size) noexcept {
    auto writeSize = static_cast<int32_t>(std::min(size, static_cast<size_t>(INT32_MAX)));
    int32_t result = SSL_write(sslPtr_.get(), data, writeSize);
    if (result <= 0) {
        lastSslError_ = SSL_get_error(sslPtr_.get(), result);
    }
    return result;
}

ssize_t TLSTransport::read(void* buffer, size_t size) noexcept {
    auto readSize = static_cast<int32_t>(std::min(size, static_cast<size_t>(INT32_MAX)));
    int32_t result = SSL_read(sslPtr_.get(), buffer, readSize);
    if (result <= 0) {
        lastSslError_ = SSL_get_error(sslPtr_.get(), result);
    }
    return result;
}

bool TLSTransport::needsRetry(ssize_t) const noexcept {
    return lastSslError_ == SSL_ERROR_WANT_READ || lastSslError_ == SSL_ERROR_WANT_WRITE;
}

FdReadyAwaiter::Interest TLSTransport::retryInterest(ssize_t) const noexcept {
    return (lastSslError_ == SSL_ERROR_WANT_READ) ? FdReadyAwaiter::Interest::Read
                                                  : FdReadyAwaiter::Interest::Write;
}

void TLSTransport::shutdown() noexcept {
    if (sslPtr_) {
        int32_t ret = SSL_shutdown(sslPtr_.get());
        if (ret == 0) {
            // 已发送 close_notify，等待对端回复（非阻塞，忽略错误）
            SSL_shutdown(sslPtr_.get());
        }
        sslPtr_.reset();
    }
}

#endif

}  // namespace slark::http