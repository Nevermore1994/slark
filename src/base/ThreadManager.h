//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

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

    static inline ThreadManager& shareInstance() {
        static ThreadManager instance;
        return instance;
    }

    ThreadManager();
    ~ThreadManager();

    void add(const std::shared_ptr<Thread>& thread) noexcept;
    void remove(const std::shared_ptr<Thread>& thread) noexcept;
    void setBlockCallback(BlockCallback callback) noexcept;

    size_t size() const noexcept;

 private:
    void monitorLoop();
    void reportRunInfo() noexcept;

    std::unordered_map<uint64_t, std::weak_ptr<Thread>> threadInfos_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    std::thread monitorThread_;
    std::atomic<bool> stopRequested_{false};
    BlockCallback blockCallback_;
};

}  // namespace slark
