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
        (void)CoroutineScheduler::instance();
    }

    ~IOContext() {
        stop();
        if (eventThread_.joinable()) {
            eventThread_.join();
        }
    }

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
        // fd 可写只代表连接尝试完成，不代表连接成功
        // 需通过 getsockopt(SO_ERROR) 校验实际结果
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
        sockaddr_in addr4{};
        addr4.sin_family = AF_INET;
        addr4.sin_port = htons(port_);
        if (inet_pton(AF_INET, host_.c_str(), &addr4.sin_addr) > 0) {
            doConnect(reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4));
            return;
        }

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
            complete();
        } else if (errno == EINPROGRESS) {
            // cleanup 只捕获 fd（值类型），不捕获 this：complete/fail 可能在定时器线程延迟触发
            auto fd = fd_;
            setCleanup([fd]() {
                IOContext::instance().unregisterContinuation(fd, FdInterest::Write);
            });
            IOContext::instance().registerContinuation(fd_, FdInterest::Write, getSharedHandle());
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
            const auto fd = fd_;
            setCleanup([fd]() {
                IOContext::instance().unregisterContinuation(fd, FdInterest::Read);
            });
            IOContext::instance().registerContinuation(fd_, FdInterest::Read, getSharedHandle());
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
            const auto fd = fd_;
            setCleanup([fd]() {
                IOContext::instance().unregisterContinuation(fd, FdInterest::Write);
            });
            IOContext::instance().registerContinuation(fd_, FdInterest::Write, getSharedHandle());
            IOContext::instance().setWritable(fd_, true);
        } else {
            fail(Error::BrokenPipe);
        }
    }

private:
    const void* buffer_;
    size_t size_;
};

class FdReadyAwaiter {
public:
    enum class Interest : uint8_t { Read, Write };

    FdReadyAwaiter(int32_t fd, Interest interest) noexcept : fd_(fd), interest_(interest) {}

    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto fdInterest = (interest_ == Interest::Read) ? FdInterest::Read : FdInterest::Write;
        resumeHandle_ = handle.promise().makeResumeHandle();
        token_ = IOContext::instance().registerContinuation(fd_, fdInterest, resumeHandle_);
        if (interest_ == Interest::Read) {
            IOContext::instance().setReadable(fd_, true);
        } else {
            IOContext::instance().setWritable(fd_, true);
        }
        return resumeHandle_ && resumeHandle_->suspend();
    }

    void await_resume() noexcept {}

private:
    EventLoop::RegistrationToken token_;
    std::shared_ptr<SharedCoroutineHandle> resumeHandle_;
    int32_t fd_;
    Interest interest_;
};

class CancellableFdReadyAwaiter {
public:
    enum class Interest : uint8_t { Read, Write };

    CancellableFdReadyAwaiter(int32_t fd,
                              Interest interest,
                              const CancellationToken& token) noexcept
        : fd_(fd), interest_(interest), token_(token) {}

    bool await_ready() const noexcept { return token_.isCancelled(); }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        const auto fdInterest = (interest_ == Interest::Read) ? FdInterest::Read : FdInterest::Write;
        resumeHandle_ = handle.promise().makeResumeHandle();
        fdToken_ = IOContext::instance().registerContinuation(fd_, fdInterest, resumeHandle_);
        if (interest_ == Interest::Read) {
            IOContext::instance().setReadable(fd_, true);
        } else {
            IOContext::instance().setWritable(fd_, true);
        }

        auto fdToken = std::make_shared<EventLoop::RegistrationToken>(std::move(fdToken_));
        registration_ = token_.registerCallback([fdToken = std::move(fdToken)]() mutable {
            fdToken->cancel();
        });

        if (!registration_.isValid()) {
            IOContext::instance().unregisterContinuation(fd_, fdInterest);
            return false;
        }
        return resumeHandle_ && resumeHandle_->suspend();
    }

    bool await_resume() noexcept { return !token_.isCancelled(); }

private:
    int32_t fd_;
    Interest interest_;
    const CancellationToken& token_;
    CancellationTokenRegistration registration_;
    EventLoop::RegistrationToken fdToken_;
    std::shared_ptr<SharedCoroutineHandle> resumeHandle_;
};

} // namespace slark::http
