// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include "MoveOnlyFunction.hpp"
#include "NonCopyable.h"

namespace slark {

struct IExecutor {
    virtual ~IExecutor() = default;
    virtual void execute(MoveOnlyFunction<void()> task) noexcept = 0;

    virtual bool isInline() const noexcept {
        return false;
    }
};

struct InlineExecutor final : IExecutor {
    void execute(MoveOnlyFunction<void()> task) noexcept override {
        if (task) {
            task();
        }
    }

    bool isInline() const noexcept override {
        return true;
    }
};

}  // namespace slark
