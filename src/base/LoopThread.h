//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "Thread.h"

namespace slark {

class LoopThread final : public Thread {
 public:
    using Loop = std::function<void(LoopThread&)>;
    using WakeHook = Thread::WakeHook;

    static std::shared_ptr<LoopThread> create(std::string name, Loop loop, WakeHook wakeHook = {});

    using Thread::drainPendingWork;
    using Thread::hasPendingWork;
    using Thread::waitForWork;

    // Wakes waitForWork waiters and the wake hook (e.g. a Channel receiver).
    void notify() noexcept;

 private:
    LoopThread(std::string name, Loop loop, WakeHook wakeHook);
    void runLoop() noexcept;

    Loop loop_;
};

}  // namespace slark
