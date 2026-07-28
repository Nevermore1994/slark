// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <coroutine>
#include <cstring>
#include <mutex>
#include <thread>

#include "AliveFlag.hpp"
#include "AsyncIOOperation.hpp"
#include "CancellationToken.h"
#include "EventLoop.h"
#include "Time.hpp"

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
    IOContext() {}

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

class AsyncConnect : public slark::AsyncIOOperationBase<AsyncConnect, void> {
 public:
    AsyncConnect(int32_t fd, std::string_view host, uint16_t port) : host_(host), port_(port) {
        fd_ = fd;
    }

    IOResult<void> await_resume() {
        auto s = getState();
        if (s == slark::IOOpState::Failed || s == slark::IOOpState::TimedOut) {
            return std::unexpected(getError().value_or(Error::NetworkError));
        }
        if (s == slark::IOOpState::Completed || s == slark::IOOpState::Pending) {
            int32_t soError = 0;
            socklen_t len = sizeof(soError);
            if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &soError, &len) == 0 && soError == 0) {
                return {};
            }
            return std::unexpected(Error::ConnectionRefused);
        }
        return {};
    }

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

    void doConnect(sockaddr* addr, socklen_t addrLen) noexcept {
        int32_t result = ::connect(fd_, addr, addrLen);
        if (result == 0) {
            complete();
        } else if (errno == EINPROGRESS) {
            setCleanup([fd = fd_]() {
                IOContext::instance().unregisterContinuation(fd, FdInterest::Write);
            });
            IOContext::instance().registerContinuation(fd_,
                                                       FdInterest::Write,
                                                       ioHandle(),
                                                       ioAliveFlag());
            IOContext::instance().setWritable(fd_, true);
        } else {
            fail(Error::ConnectionRefused);
        }
    }

 private:
    std::string host_;
    uint16_t port_;
};

class AsyncRead : public slark::AsyncIOOperationBase<AsyncRead, size_t> {
 public:
    AsyncRead(int32_t fd, void* buffer, size_t size) : buffer_(buffer), size_(size) {
        fd_ = fd;
    }

    void startOperation() noexcept override {
        ssize_t result = ::read(fd_, buffer_, size_);
        if (result > 0) {
            complete(static_cast<size_t>(result));
        } else if (result == 0) {
            fail(Error::ConnectionReset);
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            setCleanup([fd = fd_]() {
                IOContext::instance().unregisterContinuation(fd, FdInterest::Read);
            });
            IOContext::instance().registerContinuation(fd_,
                                                       FdInterest::Read,
                                                       ioHandle(),
                                                       ioAliveFlag());
            IOContext::instance().setReadable(fd_, true);
        } else {
            fail(Error::NetworkError);
        }
    }

    void storeResult(size_t value) noexcept {
        value_ = value;
    }

    size_t getResult() noexcept {
        return value_;
    }

 private:
    void* buffer_;
    size_t size_;
    size_t value_{0};
};

class AsyncWrite : public slark::AsyncIOOperationBase<AsyncWrite, size_t> {
 public:
    AsyncWrite(int32_t fd, const void* buffer, size_t size) : buffer_(buffer), size_(size) {
        fd_ = fd;
    }

    void startOperation() noexcept override {
        auto result = ::write(fd_, buffer_, size_);
        if (result >= 0) {
            complete(static_cast<size_t>(result));
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            setCleanup([fd = fd_]() {
                IOContext::instance().unregisterContinuation(fd, FdInterest::Write);
            });
            IOContext::instance().registerContinuation(fd_,
                                                       FdInterest::Write,
                                                       ioHandle(),
                                                       ioAliveFlag());
            IOContext::instance().setWritable(fd_, true);
        } else {
            fail(Error::BrokenPipe);
        }
    }

    void storeResult(size_t value) noexcept {
        value_ = value;
    }

    size_t getResult() noexcept {
        return value_;
    }

 private:
    const void* buffer_;
    size_t size_;
    size_t value_{0};
};

class FdReadyAwaiter {
 public:
    enum class Interest : uint8_t { Read, Write };

    FdReadyAwaiter(int32_t fd, Interest interest) noexcept : fd_(fd), interest_(interest) {}

    FdReadyAwaiter(FdReadyAwaiter&&) = default;
    FdReadyAwaiter& operator=(FdReadyAwaiter&&) = default;

    ~FdReadyAwaiter() {
        flag_.setDead();
        token_.cancel();
    }

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle_ = handle;
        flag_ = AliveFlag{};

        auto fdInterest = (interest_ == Interest::Read) ? FdInterest::Read : FdInterest::Write;
        token_ =
            IOContext::instance().registerContinuation(fd_, fdInterest, handle_, flag_.shared());

        if (interest_ == Interest::Read) {
            IOContext::instance().setReadable(fd_, true);
        } else {
            IOContext::instance().setWritable(fd_, true);
        }
        return true;
    }

    void await_resume() noexcept {}

 private:
    int32_t fd_;
    Interest interest_;
    std::coroutine_handle<> handle_;
    AliveFlag flag_;
    EventLoop::RegistrationToken token_;
};

template <typename Awaiter>
class CancellableAwaiter {
 public:
    CancellableAwaiter(Awaiter&& awaiter, slark::CancellationToken token) noexcept
        : awaiter_(std::move(awaiter)), token_(std::move(token)) {}

    CancellableAwaiter(CancellableAwaiter&&) = default;
    CancellableAwaiter& operator=(CancellableAwaiter&&) = default;

    bool await_ready() const noexcept {
        return token_.isCancelled() || awaiter_.await_ready();
    }

    template <typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        if (token_.isCancelled()) {
            return false;
        }
        cancelReg_ = token_.registerCallback([h = handle]() {
            h.resume();
        });
        return awaiter_.await_suspend(handle);
    }

    bool await_resume() {
        return !token_.isCancelled();
    }

 private:
    Awaiter awaiter_;
    slark::CancellationToken token_;
    slark::CancellationTokenRegistration cancelReg_;
};

template <typename Awaiter>
inline auto withCancellation(Awaiter awaiter, slark::CancellationToken token) noexcept {
    return CancellableAwaiter<Awaiter>{std::move(awaiter), std::move(token)};
}

}  // namespace slark::http
