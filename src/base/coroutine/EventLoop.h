// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "CoroutineControl.hpp"
#include "NonCopyable.h"

#ifdef __linux__
#include <sys/epoll.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#include <sys/types.h>
#endif

namespace slark {

enum class FdInterest : uint8_t { Read, Write };

class EventLoop : public NonCopyable {
 public:
    static constexpr int32_t kMaxEvents = 1024;
    static constexpr int32_t kPollTimeoutMs = 100;

    struct FdRegistration {
        std::shared_ptr<ResumeGate> gate;
    };

    EventLoop();
    ~EventLoop();

    void run() noexcept;
    void stop() noexcept;
    void wakeup() noexcept;

    bool isRunning() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    void registerFd(int32_t fd) noexcept;
    void unregisterFd(int32_t fd) noexcept;
    void setReadable(int32_t fd, bool enabled) noexcept;
    void setWritable(int32_t fd, bool enabled) noexcept;

    class RegistrationToken {
     public:
        RegistrationToken() = default;
        void cancel() noexcept;

        bool valid() const noexcept {
            return id_ != 0;
        }

     private:
        friend class EventLoop;

        RegistrationToken(EventLoop* loop,
                          uint64_t id,
                          int32_t fd,
                          FdInterest interest,
                          std::shared_ptr<ResumeGate> gate) noexcept
            : loop_(loop),
              id_(id),
              fd_(fd),
              interest_(interest),
              gate_(std::move(gate)) {}

        EventLoop* loop_{nullptr};
        uint64_t id_{0};
        int32_t fd_{-1};
        FdInterest interest_{};
        std::shared_ptr<ResumeGate> gate_;
    };

    RegistrationToken registerContinuation(int32_t fd,
                                           FdInterest interest,
                                           std::shared_ptr<ResumeGate> gate) noexcept;

    void unregisterContinuation(int32_t fd,
                                FdInterest interest,
                                std::shared_ptr<ResumeGate> expectedGate = {}) noexcept;

    int32_t nativeFd() const noexcept {
#ifdef __linux__
        return epollFd_;
#elif defined(__APPLE__)
        return kqueueFd_;
#endif
    }

 private:
    RegistrationToken createToken(int32_t fd,
                                  FdInterest interest,
                                  std::shared_ptr<ResumeGate> gate) noexcept;
    std::pair<FdRegistration, bool> takeRegistration(
        int32_t fd,
        FdInterest interest,
        const std::shared_ptr<ResumeGate>* expectedGate = nullptr) noexcept;
    void dispatchEvent(int32_t fd, bool readable, bool writable, bool error) noexcept;
    void poll() noexcept;

    struct FdSlot {
        FdRegistration readReg;
        FdRegistration writeReg;
        bool hasRead{false};
        bool hasWrite{false};
    };

#ifdef __linux__
    int32_t epollFd_{-1}, wakeupFd_{-1};
#elif defined(__APPLE__)
    int32_t kqueueFd_{-1};
#endif
    std::atomic<bool> running_{false};
    // stop() 可能在 eventThread 执行 run() 之前被调用（测试进程快速退出），
    // 该标志保证 run() 不会把 running_ 重新置 true 而永久阻塞 join()
    std::atomic<bool> stopRequested_{false};
    std::atomic<uint64_t> nextId_{1};
    mutable std::mutex fdMutex_;
    std::unordered_map<int32_t, FdSlot> fdSlots_;
    std::unordered_map<uint64_t, std::pair<int32_t, FdInterest>> tokenMap_;
#ifdef __linux__
    std::unordered_map<int32_t, uint32_t> fdEvents_;
#endif
};

}  // namespace slark
