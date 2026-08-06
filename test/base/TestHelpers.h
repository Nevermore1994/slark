//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <functional>
#include <mutex>
#include <vector>

#include "Task.hpp"

namespace slark::coro {

struct ManualExecutor : IExecutor {
    std::vector<std::function<void()>> tasks;
    std::mutex mutex;

    void execute(std::function<void()> task) noexcept override {
        std::lock_guard lock(mutex);
        tasks.push_back(std::move(task));
    }

    const char* name() const noexcept override {
        return "ManualExecutor";
    }

    void drainOne() {
        std::function<void()> task;
        {
            std::lock_guard lock(mutex);
            if (tasks.empty()) {
                return;
            }
            task = std::move(tasks.front());
            tasks.erase(tasks.begin());
        }
        if (task) {
            task();
        }
    }

    void drainAll() {
        while (true) {
            std::function<void()> task;
            {
                std::lock_guard lock(mutex);
                if (tasks.empty()) {
                    return;
                }
                task = std::move(tasks.front());
                tasks.erase(tasks.begin());
            }
            if (task) {
                task();
            }
        }
    }

    bool empty() {
        std::lock_guard lock(mutex);
        return tasks.empty();
    }
};

}  // namespace slark::coro
