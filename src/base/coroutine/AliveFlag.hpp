// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>

namespace slark {

class AliveFlag {
 public:
    using FlagType = std::atomic<bool>;
    using Ptr = std::shared_ptr<FlagType>;
    using WeakPtr = std::weak_ptr<FlagType>;

    AliveFlag() : ptr_(std::make_shared<FlagType>(true)) {}

    ~AliveFlag() {
        if (ptr_) {
            ptr_->store(false, std::memory_order_release);
            ptr_.reset();
        }
    }

    AliveFlag(AliveFlag&& other) noexcept : ptr_(std::move(other.ptr_)) {}

    AliveFlag& operator=(AliveFlag&& other) noexcept {
        if (this != &other) {
            if (ptr_) {
                ptr_->store(false, std::memory_order_release);
            }
            ptr_ = std::move(other.ptr_);
        }
        return *this;
    }

    AliveFlag(const AliveFlag&) = delete;
    AliveFlag& operator=(const AliveFlag&) = delete;

    [[nodiscard]] Ptr shared() const noexcept {
        return ptr_;
    }

    [[nodiscard]] WeakPtr weak() const noexcept {
        return ptr_ ? WeakPtr(ptr_) : WeakPtr{};
    }

    [[nodiscard]] bool isAlive() const noexcept {
        return ptr_ && ptr_->load(std::memory_order_acquire);
    }

    void setDead() noexcept {
        if (ptr_) {
            ptr_->store(false, std::memory_order_release);
        }
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

 private:
    Ptr ptr_;
};

template <typename F>
inline bool checkAlive(const AliveFlag::WeakPtr& weakFlag, F&& action) {
    auto flag = weakFlag.lock();
    if (!flag || !flag->load(std::memory_order_acquire)) {
        return false;
    }
    std::forward<F>(action)();
    return true;
}

}  // namespace slark
