//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "Error.h"
#include "MPSCQueue.hpp"
#include "MoveOnlyFunction.hpp"
#include "NonCopyable.h"
#include "Time.hpp"

namespace slark {

using ThreadTask = MoveOnlyFunction<void()>;

class Thread : public NonCopyMovable, public std::enable_shared_from_this<Thread> {
 public:
    using WakeHook = std::function<void()>;

    virtual ~Thread() noexcept;

    virtual bool isRunning() const noexcept;
    bool isStopRequested() const noexcept;
    bool hasExited() const noexcept;
    bool isCurrentThread() const noexcept;

    virtual void stop() noexcept;
    void join() noexcept;
    void notify() noexcept;

    Expected<void> postTask(ThreadTask task) noexcept;
    Expected<void> postAndWait(ThreadTask task) noexcept;

    bool isActive() const noexcept {
        return isActive_.load(std::memory_order_acquire);
    }

    Time::TimePoint getLastRunTimeStamp() const noexcept {
        return lastRunTimeStamp_.load();
    }

    std::thread::id getId() const noexcept {
        return id_.load(std::memory_order_acquire);
    }

    uint64_t serial() const noexcept {
        return serial_;
    }

    uint32_t getHashId() const noexcept {
        return static_cast<uint32_t>(std::hash<std::thread::id>{}(getId()));
    }

    std::string getName() const noexcept;
    void setThreadName(std::string_view name) noexcept;

    class ScopedInactive {
     public:
        ScopedInactive() noexcept;
        ~ScopedInactive() noexcept;
        ScopedInactive(const ScopedInactive&) = delete;
        ScopedInactive& operator=(const ScopedInactive&) = delete;

     private:
        Thread* thread_;
    };

    static void setCurrentActive(bool active) noexcept;

 protected:
    explicit Thread(std::string name, WakeHook wakeHook = {});

    void startWorker(ThreadTask body);
    void drainPendingWork() noexcept;
    bool hasPendingWork() const noexcept;
    void notifyWorker() noexcept;

    // Called on the worker thread only; the predicate is evaluated under mutex_.
    template <class Pred>
    void waitForWork(Pred&& predicate) {
        std::unique_lock lock(mutex_);
        ScopedInactive guard;
        cond_.wait(lock, [&] {
            return isStopRequested() || hasPendingWork() || predicate();
        });
    }

    void setActive(bool active) noexcept;

 private:
    static Thread* current() noexcept;
    void runWorker(ThreadTask body) noexcept;
    void setup() noexcept;
    void invokeWakeHook() noexcept;

 protected:
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    MPSCQueue<ThreadTask> taskQueue_;
    std::atomic<bool> stopRequested_{false};

 private:
    std::string name_;
    WakeHook wakeHook_;
    std::atomic<bool> exited_{false};
    std::atomic<bool> isActive_{false};
    std::atomic<bool> nameDirty_{true};
    std::atomic<std::thread::id> id_{};
    std::atomic<uint64_t> lastRunTimeStamp_{0};
    const uint64_t serial_;
    std::mutex joinMutex_;
    std::thread worker_;
};

}  // namespace slark
