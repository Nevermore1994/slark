// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <coroutine>

#include "AliveFlag.hpp"
#include "CoroutineScheduler.hpp"

namespace slark {

struct YieldAwaiter {
    ~YieldAwaiter() {
        flag_.setDead();
    }

    bool await_ready() const noexcept {
        return false;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle_ = handle;
        flag_ = AliveFlag{};
        auto weakFlag = flag_.weak();
        auto h = handle_;
        CoroutineScheduler::instance().scheduleCallback(std::chrono::milliseconds(0),
                                                        [weakFlag, h]() {
                                                            checkAlive(weakFlag, [h]() {
                                                                h.resume();
                                                            });
                                                        });
        return true;
    }

    void await_resume() noexcept {}

 private:
    std::coroutine_handle<> handle_;
    AliveFlag flag_;
};

inline YieldAwaiter yield() noexcept {
    return {};
}

}  // namespace slark
