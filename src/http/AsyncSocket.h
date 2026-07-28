//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <chrono>
#include <expected>
#include <memory>
#include <string_view>

#include "CancellationToken.h"
#include "Data.h"
#include "HttpResponseParser.h"
#include "HttpsHelper.h"
#include "IOContext.h"
#include "Socket.hpp"
#include "Task.hpp"

namespace slark::http {

using AsyncSocketResult = IOResult<size_t>;

constexpr std::chrono::milliseconds kDefaultConnectTimeout{std::chrono::seconds{10}};
constexpr std::chrono::milliseconds kDefaultReadTimeout{std::chrono::seconds{30}};
constexpr std::chrono::milliseconds kDefaultWriteTimeout{std::chrono::seconds{30}};

// Transport strategy interface for read/write abstraction
class ITransport {
 public:
    virtual ~ITransport() = default;
    virtual ssize_t write(const void* data, size_t size) noexcept = 0;
    virtual ssize_t read(void* buffer, size_t size) noexcept = 0;
    virtual bool needsRetry(ssize_t result) const noexcept = 0;
    virtual FdReadyAwaiter::Interest retryInterest(ssize_t result) const noexcept = 0;
    virtual void shutdown() noexcept = 0;
};

// Plain TCP transport
class PlainTransport final : public ITransport {
 public:
    explicit PlainTransport(Socket fd) noexcept : fd_(fd) {}

    ssize_t write(const void* data, size_t size) noexcept override {
        lastInterest_ = FdReadyAwaiter::Interest::Write;
        return ::send(fd_, data, size, kNoSignal);
    }

    ssize_t read(void* buffer, size_t size) noexcept override {
        lastInterest_ = FdReadyAwaiter::Interest::Read;
        return ::read(fd_, buffer, size);
    }

    bool needsRetry(ssize_t result) const noexcept override {
        return result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
    }

    FdReadyAwaiter::Interest retryInterest(ssize_t) const noexcept override {
        return lastInterest_;
    }

    void shutdown() noexcept override {}

 private:
    Socket fd_;
    FdReadyAwaiter::Interest lastInterest_ = FdReadyAwaiter::Interest::Write;
};

#if ENABLE_HTTPS
// TLS transport wrapping OpenSSL
class TLSTransport final : public ITransport {
 public:
    TLSTransport(Socket fd, const std::string& hostname);
    ~TLSTransport();

    /// @return true 表示握手成功，false 表示失败（不抛异常）
    Task<bool> performHandshake(std::chrono::milliseconds timeout);

    ssize_t write(const void* data, size_t size) noexcept override;
    ssize_t read(void* buffer, size_t size) noexcept override;
    bool needsRetry(ssize_t result) const noexcept override;
    FdReadyAwaiter::Interest retryInterest(ssize_t result) const noexcept override;
    void shutdown() noexcept override;

    [[nodiscard]] bool isValid() const noexcept {
        return sslPtr_ != nullptr;
    }

 private:
    Socket fd_;
    SSLPtr sslPtr_;
    mutable int32_t lastSslError_ = 0;
};
#endif

class IAsyncSocket {
 public:
    virtual ~IAsyncSocket() = default;

    virtual Task<void> asyncConnect(const AddressInfoPtr& address,
                                    std::chrono::milliseconds timeout = kDefaultConnectTimeout) = 0;

    virtual Task<AsyncSocketResult> asyncSend(
        const std::string_view& data,
        std::chrono::milliseconds timeout = kDefaultWriteTimeout) = 0;

    // 带取消支持的异步发送，用于大 body 上传或需要快速响应取消的场景
    virtual Task<AsyncSocketResult> asyncSend(const std::string_view& data,
                                              std::chrono::milliseconds timeout,
                                              CancellationToken token) = 0;

    virtual Task<AsyncSocketResult> asyncReceive(
        void* buffer,
        size_t size,
        std::chrono::milliseconds timeout = kDefaultReadTimeout) = 0;

    // 带取消支持的异步接收，用于需要快速响应取消的场景
    virtual Task<AsyncSocketResult> asyncReceive(void* buffer,
                                                 size_t size,
                                                 std::chrono::milliseconds timeout,
                                                 CancellationToken token) = 0;

    virtual Task<DataPtr> asyncReceiveAll(
        std::chrono::milliseconds timeout = kDefaultReadTimeout) = 0;

    // 流式接收：边读边喂 parser，触发 onHeader / onData 回调
    // token 参数用于在流式接收过程中检查取消状态，实现快速响应
    virtual Task<bool> asyncReceiveStreaming(
        HttpResponseParser& parser,
        std::chrono::milliseconds timeout = kDefaultReadTimeout,
        CancellationToken token = CancellationToken{}) = 0;

    virtual void close() noexcept = 0;
    [[nodiscard]] virtual bool isValid() const noexcept = 0;
    [[nodiscard]] virtual Socket getFd() const noexcept = 0;

    // 设置性能统计对象（用于请求级性能追踪）
    virtual void setPerf(RequestPerf* perf) noexcept = 0;
    virtual void setTransport(std::unique_ptr<ITransport> transport) noexcept = 0;
};

// Unified async socket using ITransport strategy
class AsyncSocket final : public IAsyncSocket, public NonCopyable {
 public:
    explicit AsyncSocket(IPVersion ipVersion, std::unique_ptr<ITransport> transport = nullptr);
    ~AsyncSocket();

    Task<void> asyncConnect(const AddressInfoPtr& address,
                            std::chrono::milliseconds timeout = kDefaultConnectTimeout) override;

    Task<AsyncSocketResult> asyncSend(
        const std::string_view& data,
        std::chrono::milliseconds timeout = kDefaultWriteTimeout) override;

    Task<AsyncSocketResult> asyncSend(const std::string_view& data,
                                      std::chrono::milliseconds timeout,
                                      CancellationToken token) override;

    Task<AsyncSocketResult> asyncReceive(
        void* buffer,
        size_t size,
        std::chrono::milliseconds timeout = kDefaultReadTimeout) override;

    Task<AsyncSocketResult> asyncReceive(void* buffer,
                                         size_t size,
                                         std::chrono::milliseconds timeout,
                                         CancellationToken token) override;

    Task<DataPtr> asyncReceiveAll(std::chrono::milliseconds timeout = kDefaultReadTimeout) override;

    Task<bool> asyncReceiveStreaming(HttpResponseParser& parser,
                                     std::chrono::milliseconds timeout = kDefaultReadTimeout,
                                     CancellationToken token = CancellationToken{}) override;

    void close() noexcept override;

    [[nodiscard]] bool isValid() const noexcept override {
        return socket_ != kInvalidSocket;
    }

    [[nodiscard]] Socket getFd() const noexcept override {
        return socket_;
    }

    void setTransport(std::unique_ptr<ITransport> transport) noexcept override {
        transport_ = std::move(transport);
    }

    void setPerf(RequestPerf* perf) noexcept override {
        perf_ = perf;
    }

 private:
    [[maybe_unused]] IPVersion ipVersion_;
    Socket socket_;
    std::unique_ptr<ITransport> transport_;
    RequestPerf* perf_ = nullptr;
};

inline std::unique_ptr<IAsyncSocket> makeSocket(IPVersion ipVersion) {
    return std::make_unique<AsyncSocket>(ipVersion);
}

}  // namespace slark::http
