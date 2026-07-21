//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "NonCopyable.h"
#include "SharedCoroutineHandle.hpp"

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <unordered_map>

#ifdef __linux__
#include <sys/epoll.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#include <sys/types.h>
#endif

namespace slark {

enum class FdInterest : uint8_t {
    Read,
    Write,
};

class EventLoop : public NonCopyable {
public:
    static constexpr int32_t kMaxEvents = 1024;
    static constexpr int32_t kPollTimeoutMs = 100;

    using FdCallback = std::function<void(int32_t fd, bool readable, bool writable, bool error)>;

    EventLoop();
    ~EventLoop();

    void run() noexcept;
    void stop() noexcept;
    void wakeup() noexcept;

    [[nodiscard]] bool isRunning() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    void registerFd(int32_t fd) noexcept;
    void unregisterFd(int32_t fd) noexcept;
    void setReadable(int32_t fd, bool enabled) noexcept;
    void setWritable(int32_t fd, bool enabled) noexcept;

    class RegistrationToken {
    public:
        RegistrationToken() = default;

        // 取消路径以同一摘除操作赢得恢复权：摘除成功者负责恢复。
        void cancel() noexcept;

        [[nodiscard]] bool valid() const noexcept {
            return loop_ != nullptr && handle_ != nullptr && !handle_->done();
        }

    private:
        friend class EventLoop;

        RegistrationToken(EventLoop* loop,
                          std::shared_ptr<SharedCoroutineHandle> handle,
                          int32_t fd,
                          FdInterest interest) noexcept
            : loop_(loop), handle_(std::move(handle)), fd_(fd), interest_(interest) {}

        EventLoop* loop_{nullptr};
        std::shared_ptr<SharedCoroutineHandle> handle_;
        int32_t fd_{-1};
        FdInterest interest_{};
    };

    RegistrationToken registerContinuation(
        int32_t fd,
        FdInterest interest,
        std::shared_ptr<SharedCoroutineHandle> handle) noexcept;

    void unregisterContinuation(int32_t fd, FdInterest interest) noexcept;
    void unregisterAllContinuations(int32_t fd) noexcept;

    [[nodiscard]] int32_t nativeFd() const noexcept {
#ifdef __linux__
        return epollFd_;
#elif defined(__APPLE__)
        return kqueueFd_;
#endif
    }

private:
    struct FdWaiters {
        std::shared_ptr<SharedCoroutineHandle> readHandle;
        std::shared_ptr<SharedCoroutineHandle> writeHandle;

        [[nodiscard]] bool empty() const noexcept {
            return !readHandle && !writeHandle;
        }
    };

    [[nodiscard]] std::shared_ptr<SharedCoroutineHandle> takeContinuation(
        int32_t fd,
        FdInterest interest,
        bool& needDisable,
        const std::shared_ptr<SharedCoroutineHandle>& expected = nullptr) noexcept;
    void poll() noexcept;
    void dispatchEvent(int32_t fd, bool readable, bool writable, bool error) noexcept;

#ifdef __linux__
    int32_t epollFd_{-1};
    int32_t wakeupFd_{-1};
#elif defined(__APPLE__)
    int32_t kqueueFd_{-1};
#endif
    std::atomic<bool> running_{false};

    mutable std::mutex fdMutex_;
    std::unordered_map<int32_t, FdWaiters> continuations_;

#ifdef __linux__
    std::unordered_map<int32_t, uint32_t> fdEvents_;
#endif
};

} // namespace slark
