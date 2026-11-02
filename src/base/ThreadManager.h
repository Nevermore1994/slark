//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "Thread.h"

namespace slark {

static const Time::TimeDelta kMaxThreadBlockTimeInterval = Time::TimeDelta::fromSeconds(30.0);

class ThreadManager final : public NonCopyMovable {
 public:
    using BlockCallback =
        std::function<void(std::string_view threadName, Time::TimeDelta blockDuration)>;

    struct Config {
        std::chrono::milliseconds pollInterval{10'000};
        Time::TimeDelta blockThreshold = kMaxThreadBlockTimeInterval;
    };

    /// Never destroyed: worker threads touch it while the process exits.
    static ThreadManager& shareInstance();

    ThreadManager();
    explicit ThreadManager(Config config);
    ~ThreadManager();

    /// Returns true when the thread was not registered yet.
    bool add(const std::shared_ptr<Thread>& thread) noexcept;
    /// Returns true when the thread was registered.
    bool remove(const std::shared_ptr<Thread>& thread) noexcept;
    void setBlockCallback(BlockCallback callback) noexcept;

    /// Registered threads that still exist.
    size_t size() const noexcept;

 private:
    void monitorLoop();
    void reportRunInfo() noexcept;

    const Config config_;
    std::unordered_map<uint64_t, std::weak_ptr<Thread>> threadInfos_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    bool stopRequested_ = false;
    BlockCallback blockCallback_;
    // Must stay last: the thread starts in the initializer list and reads the members above.
    std::thread monitorThread_;
};

}  // namespace slark
