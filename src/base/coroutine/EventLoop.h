//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "NonCopyable.h"

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <functional>
#include <mutex>
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
    ~EventLoop() ;

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

    // 按 interest 注册 / 注销 continuation，同一 fd 可同时有 read 和 write 等待者
    void registerContinuation(int32_t fd,
                              FdInterest interest,
                              std::coroutine_handle<> handle) noexcept;
    void unregisterContinuation(int32_t fd, FdInterest interest) noexcept;
    void unregisterAllContinuations(int32_t fd) noexcept;

    // 设置自定义的协程调度器，如果不设置，默认使用 CoroutineScheduler::instance()
    void setScheduler(std::function<void(std::coroutine_handle<>)> scheduler) noexcept {
        scheduler_ = std::move(scheduler);
    }

    [[nodiscard]] int32_t nativeFd() const noexcept {
#ifdef __linux__
        return epollFd_;
#elif defined(__APPLE__)
        return kqueueFd_;
#endif
    }

private:
    struct FdWaiters {
        std::coroutine_handle<> readHandle;
        std::coroutine_handle<> writeHandle;

        [[nodiscard]] bool empty() const noexcept {
            return !readHandle && !writeHandle;
        }
    };

    // 事件关注状态跟踪，用于收敛无效事件
    struct FdInterestState {
        bool hasReadWaiter = false;
        bool hasWriteWaiter = false;
    };

    struct ExtractedHandles {
        std::coroutine_handle<> readHandle;
        std::coroutine_handle<> writeHandle;
        bool needDisableRead = false;
        bool needDisableWrite = false;
    };

    void poll() noexcept;
    void dispatchEvent(int32_t fd, bool readable, bool writable, bool error) noexcept;

    // 从 continuation 映射中提取等待句柄
    [[nodiscard]] ExtractedHandles extractHandles(int32_t fd,
                                                  bool readable,
                                                  bool writable,
                                                  bool error) noexcept;

    // 更新事件关注状态并返回需要禁用的事件
    void updateInterestState(int32_t fd, bool needDisableRead, bool needDisableWrite) noexcept;

    // 恢复协程
    void resumeCoroutines(std::coroutine_handle<> readHandle,
                          std::coroutine_handle<> writeHandle) noexcept;

    std::function<void(std::coroutine_handle<>)> scheduler_;

#ifdef __linux__
    int32_t epollFd_{-1};
#elif defined(__APPLE__)
    int32_t kqueueFd_{-1};
#endif

    // 用于唤醒 poll 的 fd（eventfd 或 pipe）
    int32_t wakeupFd_{-1};
    std::atomic<bool> running_{false};
    mutable std::mutex continuationMutex_;
    std::unordered_map<int32_t, FdWaiters> continuations_;

    // 事件关注状态跟踪
    mutable std::mutex interestMutex_;
    std::unordered_map<int32_t, FdInterestState> interestStates_;

#ifdef __linux__
    std::mutex fdEventsMutex_;
    std::unordered_map<int32_t, uint32_t> fdEvents_;
#endif
};

} // namespace slark
