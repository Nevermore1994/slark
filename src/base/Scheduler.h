//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "Executor.h"
#include "MoveOnlyFunction.hpp"
#include "NonCopyable.h"
#include "Time.hpp"
#include "Timer.h"

namespace slark {

class TaskThread;

// A slot holds at most one pending timer; postAt on a used slot replaces it.
class Scheduler : public IExecutor {
 public:
    using Task = MoveOnlyFunction<void()>;
    using Slot = uint32_t;

    virtual void postAt(Slot slot, Time::TimePoint at, Task task) = 0;
    virtual void cancel(Slot slot) = 0;
    [[nodiscard]] virtual Time::TimePoint now() const noexcept = 0;
    [[nodiscard]] virtual bool onSchedulerThread() const noexcept = 0;
};

class ThreadScheduler final : public Scheduler, public NonCopyable {
 public:
    explicit ThreadScheduler(std::string name);
    ~ThreadScheduler() override;

    [[nodiscard]] bool valid() const noexcept {
        return thread_ != nullptr;
    }

    // Safe to call from a task running on this scheduler: it then does not join itself.
    void stop() noexcept;

    bool tryExecute(Task task) noexcept override;
    void execute(Task task) noexcept override;
    void postAt(Slot slot, Time::TimePoint at, Task task) override;
    void cancel(Slot slot) override;
    [[nodiscard]] Time::TimePoint now() const noexcept override;
    [[nodiscard]] bool onSchedulerThread() const noexcept override;

 private:
    std::shared_ptr<TaskThread> thread_;
    std::mutex mutex_;
    std::unordered_map<Slot, TimerId> slots_;
};

}  // namespace slark
