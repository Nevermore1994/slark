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

    [[nodiscard]] size_t fdCount() const noexcept {
        std::lock_guard<std::mutex> lock(fdMutex_);
        return fdSlots_.size();
    }

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

    // Registers the waiter and installs kernel interest as the awaiter's setup step, so
    // nothing can run between them. A concurrent unregister/replace no longer leaves the
    // waiter parked: it is woken with a terminal notification instead of a silent disarm.
    RegistrationToken registerContinuationWithInterest(int32_t fd,
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

    // Diagnostics: the generation of an fd's current registration lifetime, 0 when unregistered.
    [[nodiscard]] uint64_t fdGeneration(int32_t fd) const noexcept;

    // Replays one kernel event with an explicit generation (tests and event replay).
    void dispatchFdEvent(int32_t fd, bool readable, bool writable, uint64_t generation) noexcept;

 private:
    RegistrationToken createToken(int32_t fd,
                                  FdInterest interest,
                                  std::shared_ptr<ResumeGate> gate) noexcept;
    std::pair<FdRegistration, bool> takeRegistration(
        int32_t fd,
        FdInterest interest,
        const std::shared_ptr<ResumeGate>* expectedGate = nullptr) noexcept;
    [[nodiscard]] uint64_t ensureGenerationLocked(int32_t fd) noexcept;
    [[nodiscard]] bool matchesGeneration(int32_t fd, uint64_t generation) const noexcept;
    void dispatchEvent(int32_t fd, bool readable, bool writable, bool error,
                       uint64_t generation) noexcept;
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
    // stop() may be called before run() executes on the event thread (e.g. fast test exit);
    // this flag ensures run() does not set running_ back to true and block join() forever
    std::atomic<bool> stopRequested_{false};
    std::atomic<uint64_t> nextId_{1};
    mutable std::mutex fdMutex_;
    std::unordered_map<int32_t, FdSlot> fdSlots_;
    std::unordered_map<int32_t, uint64_t> fdGenerations_;
    std::unordered_map<uint64_t, std::pair<int32_t, FdInterest>> tokenMap_;
#ifdef __linux__
    std::unordered_map<int32_t, uint32_t> fdEvents_;
#endif
};

}  // namespace slark
