//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AsyncIOOperation.hpp"
#include "Awaitable.hpp"
#include "CancellationToken.h"
#include "Coroutine.hpp"
#include "CoroutineScheduler.hpp"
#include "EventLoop.h"
#include "Time.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <coroutine>
#include <cstring>
#include <mutex>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

namespace slark::http {

using slark::IOResult;

class IOContext : public slark::EventLoop {
public:
    static IOContext& instance() noexcept;

    void registerSocket(int32_t fd) noexcept {
        ensureRunning();
        registerFd(fd);
    }

    void unregisterSocket(int32_t fd) noexcept {
        unregisterFd(fd);
    }

private:
    IOContext() {
        // 确保 CoroutineScheduler 先于 IOContext 构造
        // 这样 IOContext 会先于 CoroutineScheduler 析构，避免悬空引用
        (void)CoroutineScheduler::instance();
    }

    ~IOContext() override {
        stop();
        if (eventThread_.joinable()) {
            eventThread_.join();
        }
        // 清理所有残留的 continuations，避免悬空句柄
    }

    // 延迟启动事件循环线程
    void ensureRunning() noexcept {
        if (!isRunning()) {
            std::call_once(startFlag_, [this]() {
                eventThread_ = std::thread([this]() {
                    run();
                });
            });
        }
    }

    std::thread eventThread_;
    std::once_flag startFlag_;
};

class AsyncConnect : public slark::AsyncIOOperation<void> {
public:
    AsyncConnect(int32_t fd, std::string_view host, uint16_t port) : host_(host), port_(port) {
        fd_ = fd;
    }

    IOResult<void> await_resume() {
        auto s = getState();
        if (s == slark::detail::CompletionState::Failed ||
            s == slark::detail::CompletionState::TimedOut) {
            return std::unexpected(getError().value_or(Error::NetworkError));
        }
        // 异步连接完成时，必须通过 getsockopt(SO_ERROR) 校验实际连接结果
        // fd 可写只代表连接尝试完成，不代表连接成功
        // Completed: 立即连接成功 或 EINPROGRESS 后 fd 可写
        // Pending: EINPROGRESS 后 fd 可写（协程恢复但 state 未更新）
        if (s == slark::detail::CompletionState::Completed ||
            s == slark::detail::CompletionState::Pending) {
            int32_t soError = 0;
            socklen_t len = sizeof(soError);
            if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &soError, &len) == 0 && soError == 0) {
                return {};
            }
            return std::unexpected(Error::ConnectionRefused);
        }
        return {};
    }

protected:
    void startOperation() noexcept override {
        // 尝试 IPv4
        sockaddr_in addr4{};
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons(port_);
        if (inet_pton(AF_INET, host_.c_str(), &addr4.sin_addr) > 0) {
            doConnect(reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4));
            return;
        }

        // 尝试 IPv6
        sockaddr_in6 addr6{};
        addr6.sin6_family = AF_INET6;
        addr6.sin6_port = htons(port_);
        if (inet_pton(AF_INET6, host_.c_str(), &addr6.sin6_addr) > 0) {
            doConnect(reinterpret_cast<sockaddr*>(&addr6), sizeof(addr6));
            return;
        }

        fail(Error::InvalidSocket);
    }

private:
    void doConnect(sockaddr* addr, socklen_t addrLen) noexcept {
        int32_t result = ::connect(fd_, addr, addrLen);
        if (result == 0) {
            // 立即连接成功（罕见），直接标记完成
            // await_resume() 仍会通过 SO_ERROR 二次校验
            complete();
        } else if (errno == EINPROGRESS) {
            // 异步连接中，注册写事件等待可写
            // 保持 state 为 Pending，让协程挂起等待
            // 当 fd 可写时，协程恢复，在 await_resume 中通过 SO_ERROR 判断最终结果
            IOContext::instance().registerContinuation(fd_, FdInterest::Write, getHandle());
            IOContext::instance().setWritable(fd_, true);
        } else {
            fail(Error::ConnectionRefused);
        }
    }

    std::string host_;
    uint16_t port_;
};

class AsyncRead : public slark::AsyncIOOperation<size_t> {
public:
    AsyncRead(int32_t fd, void* buffer, size_t size) : buffer_(buffer), size_(size) {
        fd_ = fd;
    }

protected:
    void startOperation() noexcept override {
        ssize_t result = ::read(fd_, buffer_, size_);
        if (result > 0) {
            complete(static_cast<size_t>(result));
        } else if (result == 0) {
            fail(Error::ConnectionReset);
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            IOContext::instance().registerContinuation(fd_, FdInterest::Read, getHandle());
            IOContext::instance().setReadable(fd_, true);
        } else {
            fail(Error::NetworkError);
        }
    }

private:
    void* buffer_;
    size_t size_;
};

class AsyncWrite : public slark::AsyncIOOperation<size_t> {
public:
    AsyncWrite(int32_t fd, const void* buffer, size_t size) : buffer_(buffer), size_(size) {
        fd_ = fd;
    }

protected:
    void startOperation() noexcept override {
        auto result = ::write(fd_, buffer_, size_);
        if (result >= 0) {
            complete(static_cast<size_t>(result));
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            IOContext::instance().registerContinuation(fd_, FdInterest::Write, getHandle());
            IOContext::instance().setWritable(fd_, true);
        } else {
            fail(Error::BrokenPipe);
        }
    }

private:
    const void* buffer_;
    size_t size_;
};

// fd 就绪 awaiter：注册 fd 事件，就绪时恢复协程（替代 TimerAwaiter 轮询）
class FdReadyAwaiter {
public:
    enum class Interest : uint8_t { Read, Write };

    FdReadyAwaiter(int32_t fd, Interest interest) noexcept : fd_(fd), interest_(interest) {}

    // NOLINTNEXTLINE(readability-identifier-naming)
    bool await_ready() const noexcept {
        return false;
    }

    // NOLINTNEXTLINE(readability-identifier-naming)
    void await_suspend(std::coroutine_handle<> handle) noexcept {
        auto fdInterest = (interest_ == Interest::Read) ? FdInterest::Read : FdInterest::Write;
        IOContext::instance().registerContinuation(fd_, fdInterest, handle);
        if (interest_ == Interest::Read) {
            IOContext::instance().setReadable(fd_, true);
        } else {
            IOContext::instance().setWritable(fd_, true);
        }
    }

    // NOLINTNEXTLINE(readability-identifier-naming)
    void await_resume() noexcept {}

private:
    int32_t fd_;
    Interest interest_;
};

// 支持取消的 fd 就绪 awaiter：取消时主动唤醒协程
class CancellableFdReadyAwaiter {
public:
    enum class Interest : uint8_t { Read, Write };

    CancellableFdReadyAwaiter(int32_t fd,
                              Interest interest,
                              const CancellationToken& token) noexcept
        : fd_(fd), interest_(interest), token_(token) {}

    // NOLINTNEXTLINE(readability-identifier-naming)
    bool await_ready() const noexcept {
        return token_.isCancelled();
    }

    // NOLINTNEXTLINE(readability-identifier-naming)
    void await_suspend(std::coroutine_handle<> handle) noexcept {
        auto fdInterest = (interest_ == Interest::Read) ? FdInterest::Read : FdInterest::Write;

        // 先注册 continuation，确保 fd 注册完成后再注册取消回调
        // 这样取消回调触发时，一定能在 event loop 中找到并移除 continuation
        IOContext::instance().registerContinuation(fd_, fdInterest, handle);
        if (interest_ == Interest::Read) {
            IOContext::instance().setReadable(fd_, true);
        } else {
            IOContext::instance().setWritable(fd_, true);
        }

        // 注册取消回调，取消时恢复协程
        registration_ = token_.registerCallback([fd = fd_, interest = interest_, handle]() {
            // 取消时先移除 fd 注册，再恢复协程
            auto fdInterest = (interest == Interest::Read) ? FdInterest::Read : FdInterest::Write;
            IOContext::instance().unregisterContinuation(fd, fdInterest);
            CoroutineScheduler::instance().schedule(handle);
        });

        if (!registration_.isValid()) {
            // 已经取消，需要先移除刚注册的 continuation，再恢复协程
            IOContext::instance().unregisterContinuation(fd_, fdInterest);
            CoroutineScheduler::instance().schedule(handle);
            return;
        }
    }

    // NOLINTNEXTLINE(readability-identifier-naming)
    bool await_resume() noexcept {
        return !token_.isCancelled();
    }

private:
    int32_t fd_;
    Interest interest_;
    const CancellationToken& token_;
    CancellationTokenRegistration registration_; // 持有注册句柄，确保生命周期覆盖等待过程
};

} // namespace slark::http
