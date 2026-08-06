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
#include <utility>

#include "AliveFlag.hpp"
#include "AsyncIOOperation.hpp"
#include "CancellationToken.h"
#include "EventLoop.h"
#include "Socket.hpp"
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
    AsyncConnect(int32_t fd, ConnectAddress address) : address_(std::move(address)) {
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
        if (!address_.isValid()) {
            fail(Error::InvalidSocket);
            return;
        }
        doConnect(reinterpret_cast<const sockaddr*>(&address_.storage), address_.length);
    }

    void doConnect(const sockaddr* addr, socklen_t addrLen) noexcept {
        int32_t result = ::connect(fd_, addr, addrLen);
        if (result == 0) {
            complete();
        } else if (errno == EINPROGRESS) {
            setCleanup([fd = fd_, gate = ioGate()]() {
                IOContext::instance().unregisterContinuation(fd, FdInterest::Write, gate);
            });
            IOContext::instance().registerContinuation(fd_, FdInterest::Write, ioGate());
            IOContext::instance().setWritable(fd_, true);
        } else {
            fail(Error::ConnectionRefused);
        }
    }

 private:
    ConnectAddress address_;
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
            setCleanup([fd = fd_, gate = ioGate()]() {
                IOContext::instance().unregisterContinuation(fd, FdInterest::Read, gate);
            });
            IOContext::instance().registerContinuation(fd_, FdInterest::Read, ioGate());
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
            setCleanup([fd = fd_, gate = ioGate()]() {
                IOContext::instance().unregisterContinuation(fd, FdInterest::Write, gate);
            });
            IOContext::instance().registerContinuation(fd_, FdInterest::Write, ioGate());
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
        cancel();
    }

    bool await_ready() const noexcept {
        return false;
    }

    void setResumeGate(std::shared_ptr<ResumeGate> gate) noexcept {
        gate_ = std::move(gate);
    }

    void cancel() noexcept {
        if (gate_) {
            gate_->disarm();
        }
        token_.cancel();
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        if (!gate_) {
            gate_ = makeResumeGate(handle);
        }

        auto fdInterest = (interest_ == Interest::Read) ? FdInterest::Read : FdInterest::Write;
        token_ = IOContext::instance().registerContinuation(fd_, fdInterest, gate_);

        if (interest_ == Interest::Read) {
            IOContext::instance().setReadable(fd_, true);
        } else {
            IOContext::instance().setWritable(fd_, true);
        }
        return gate_->arm();
    }

    void await_resume() noexcept {}

 private:
    int32_t fd_;
    Interest interest_;
    std::shared_ptr<ResumeGate> gate_;
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
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        if (token_.isCancelled()) {
            return false;
        }
        gate_ = makeResumeGate(handle);
        if constexpr (requires(Awaiter& awaiter) { awaiter.setResumeGate(gate_); }) {
            awaiter_.setResumeGate(gate_);
        }
        auto weakGate = std::weak_ptr<ResumeGate>(gate_);
        cancelReg_ = token_.registerCallback([weakGate]() {
            if (auto gate = weakGate.lock()) {
                gate->signal();
            }
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
    std::shared_ptr<ResumeGate> gate_;
};

template <typename Awaiter>
inline auto withCancellation(Awaiter awaiter, slark::CancellationToken token) noexcept {
    return CancellableAwaiter<Awaiter>{std::move(awaiter), std::move(token)};
}

}  // namespace slark::http
