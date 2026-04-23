//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Log.hpp"

#include <exception>
#include <functional>
#include <utility>

namespace slark {

class ScopeGuard {
public:
    template <typename F>
    explicit ScopeGuard(F&& fn) : fn_(std::forward<F>(fn)) {}

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    ScopeGuard(ScopeGuard&& other) noexcept = delete;

    ScopeGuard& operator=(ScopeGuard&& other) = delete;

    ~ScopeGuard() {
        try {
            release();
        } catch (const std::exception& e) {
            LogE("ScopeGuard::~ScopeGuard() failed:{}", e.what());
        }
    }

    void dismiss() noexcept {
        active_ = false;
    }

    void release() {
        if (active_ && fn_) {
            fn_();
        }
        active_ = false;
        fn_ = nullptr; // 防止double free
    }

private:
    std::function<void()> fn_;
    bool active_{true};
};

} // namespace slark