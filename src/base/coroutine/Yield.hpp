// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <chrono>
#include <coroutine>
#include <memory>
#include <optional>

#include "CoroutineControl.hpp"
#include "CoroutineScheduler.hpp"
#include "Error.h"
#include "ExternalWaitBase.hpp"

namespace slark {

struct YieldAwaiter final : ExternalWaitBase<YieldAwaiter, ScheduledCallback> {
    [[nodiscard]] bool ready() const noexcept {
        return false;
    }

    [[nodiscard]] std::optional<ScheduledCallback> start(const std::shared_ptr<ResumeGate>& gate) {
        return scheduleGateSignal(std::chrono::milliseconds(0), gate);
    }

    void finish() const noexcept {}

    static const char* cancelMessage() noexcept {
        return "Yield cancelled";
    }
};

inline YieldAwaiter yield() noexcept {
    return {};
}

}  // namespace slark
