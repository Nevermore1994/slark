// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <arpa/inet.h>
#include <cerrno>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <coroutine>
#include <cstring>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>

#include "AsyncIOOperation.hpp"
#include "CancellationToken.h"
#include "ExternalWaitBase.hpp"
#include "EventLoop.h"
#include "Socket.hpp"
#include "Time.hpp"

namespace slark::http {

using slark::IOResult;

inline Error mapConnectErrno(int error) noexcept {
    switch (error) {
        case ETIMEDOUT:
            return Error::ConnectionTimeout;
        case ECONNREFUSED:
            return Error::ConnectionRefused;
        default:
            break;
    }
    return mapCommonSocketErrno(error).value_or(Error::SocketError);
}

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
        if (s == slark::IOOpState::Failed || s == slark::IOOpState::TimedOut ||
            s == slark::IOOpState::Cancelled) {
            return std::unexpected(getError().value_or(Error::NetworkError));
        }
        if (s == slark::IOOpState::Completed || s == slark::IOOpState::Waiting ||
            s == slark::IOOpState::Pending) {
            int32_t soError = 0;
            socklen_t len = sizeof(soError);
            if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &soError, &len) == 0 && soError == 0) {
                return {};
            }
            if (soError != 0) {
                return std::unexpected(mapConnectErrno(soError));
            }
            return std::unexpected(mapConnectErrno(errno));
        }
        return std::unexpected(Error::NetworkError);
    }

    void startOperation() noexcept override {
        if (!address_.isValid()) {
            fail(Error::InvalidSocket);
            return;
        }
        doConnect(reinterpret_cast<const sockaddr*>(&address_.storage), address_.length);
    }

    void doConnect(const sockaddr* addr, socklen_t addrLen) noexcept {
        while (true) {
            int32_t result = ::connect(fd_, addr, addrLen);
            if (result == 0) {
                complete();
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EINPROGRESS || errno == EALREADY || errno == EAGAIN) {
                setCleanup([fd = fd_, gate = ioGate()]() {
                    IOContext::instance().unregisterContinuation(fd, FdInterest::Write, gate);
                });
                IOContext::instance().registerContinuationWithInterest(fd_, FdInterest::Write,
                                                                      ioGate());
                return;
            }
            fail(mapConnectErrno(errno));
            return;
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
        while (true) {
            ssize_t result = ::read(fd_, buffer_, size_);
            if (result > 0) {
                complete(static_cast<size_t>(result));
                return;
            }
            if (result == 0) {
                fail(Error::ConnectionReset);
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                setCleanup([fd = fd_, gate = ioGate()]() {
                    IOContext::instance().unregisterContinuation(fd, FdInterest::Read, gate);
                });
                IOContext::instance().registerContinuationWithInterest(fd_, FdInterest::Read,
                                                                      ioGate());
                return;
            }
            fail(Error::NetworkError);
            return;
        }
    }

    void storeResult(size_t value) noexcept {
        value_ = value;
    }

    size_t getResult() noexcept {
        return value_;
    }

    // Reached after readiness: the read is attempted once more. Still-empty input is WouldBlock.
    IOResult<size_t> resumeAfterReadiness() {
        const ssize_t result = ::read(fd_, buffer_, size_);
        if (result > 0) {
            return static_cast<size_t>(result);
        }
        if (result == 0) {
            return std::unexpected(Error::ConnectionReset);
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return std::unexpected(Error::WouldBlock);
        }
        return std::unexpected(Error::NetworkError);
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
        while (true) {
            auto result = ::write(fd_, buffer_, size_);
            if (result >= 0) {
                complete(static_cast<size_t>(result));
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                setCleanup([fd = fd_, gate = ioGate()]() {
                    IOContext::instance().unregisterContinuation(fd, FdInterest::Write, gate);
                });
                IOContext::instance().registerContinuationWithInterest(fd_, FdInterest::Write,
                                                                      ioGate());
                return;
            }
            fail(Error::BrokenPipe);
            return;
        }
    }

    void storeResult(size_t value) noexcept {
        value_ = value;
    }

    size_t getResult() noexcept {
        return value_;
    }

    // Reached after readiness: the write is attempted once more. Still-full output is WouldBlock.
    IOResult<size_t> resumeAfterReadiness() {
        const auto result = ::write(fd_, buffer_, size_);
        if (result >= 0) {
            return static_cast<size_t>(result);
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return std::unexpected(Error::WouldBlock);
        }
        return std::unexpected(Error::BrokenPipe);
    }

private:
    const void* buffer_;
    size_t size_;
    size_t value_{0};
};

// Readiness of one fd, optionally bounded by a deadline. The fd continuation and the deadline
// both signal the same gate: readiness resumes the frame, an elapsed deadline reports "not
// ready". Cancellation of the frame's token raises CancelledException.
struct FdRegistration final {
    FdRegistration(EventLoop::RegistrationToken registration, slark::ScheduledCallback timer) noexcept
        : token(std::move(registration)), deadline(std::move(timer)) {}

    FdRegistration(FdRegistration&& other) noexcept
        : token(std::exchange(other.token, {})), deadline(std::move(other.deadline)) {}

    FdRegistration& operator=(FdRegistration&& other) noexcept {
        if (this != &other) {
            unregister();
            token = std::exchange(other.token, {});
            deadline = std::move(other.deadline);
        }
        return *this;
    }

    FdRegistration(const FdRegistration&) = delete;
    FdRegistration& operator=(const FdRegistration&) = delete;

    ~FdRegistration() {
        unregister();
    }

    void unregister() noexcept {
        if (token.valid()) {
            token.cancel();
            token = {};
        }
    }

    EventLoop::RegistrationToken token;
    slark::ScheduledCallback deadline;
};

class FdReadyAwaiter final
    : public slark::ExternalWaitBase<FdReadyAwaiter, FdRegistration, bool> {
 public:
    enum class Interest : uint8_t { Read, Write };

    FdReadyAwaiter(int32_t fd,
                   Interest interest,
                   std::optional<std::chrono::milliseconds> timeout = {}) noexcept
        : fd_(fd), interest_(interest), timeout_(timeout) {}

    [[nodiscard]] bool ready() const noexcept {
        return false;
    }

    [[nodiscard]] std::optional<FdRegistration> start(const std::shared_ptr<ResumeGate>& gate) {
        const auto fdInterest = (interest_ == Interest::Read) ? FdInterest::Read : FdInterest::Write;
        auto registration =
            IOContext::instance().registerContinuationWithInterest(fd_, fdInterest, gate);
        slark::ScheduledCallback deadline;
        if (timeout_) {
            deadline = slark::ScheduledCallback(slark::CoroutineScheduler::instance().scheduleCallback(
                *timeout_, [weak = std::weak_ptr<ResumeGate>(gate)]() {
                    if (auto locked = weak.lock()) {
                        locked->signalTerminal();
                    }
                }));
        }
        return FdRegistration(std::move(registration), std::move(deadline));
    }

    bool finish() {
        // A terminal notification means the wait was abandoned, not satisfied.
        return gate() && !gate()->consumeNotReady();
    }

    static std::string_view cancelMessage() noexcept {
        return "Fd wait cancelled";
    }

 private:
    int32_t fd_;
    Interest interest_;
    std::optional<std::chrono::milliseconds> timeout_;
};

}  // namespace slark::http
