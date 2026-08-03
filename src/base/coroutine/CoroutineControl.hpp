// Copyright (c) 2026 Nevermore-. All rights reserved.

#pragma once

#include <condition_variable>
#include <cstdint>
#include <coroutine>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace slark {

class CoroutineFrameControl final {
 public:
    CoroutineFrameControl() = default;

    CoroutineFrameControl(const CoroutineFrameControl&) = delete;
    CoroutineFrameControl& operator=(const CoroutineFrameControl&) = delete;

    bool resume(std::coroutine_handle<> handle) noexcept {
        std::coroutine_handle<> deferredHandle;
        {
            std::unique_lock lock(mutex_);
            if (destroyed_ || !handle || handle.done()) {
                return false;
            }
            ++activeResumes_;
            activeThread_ = std::this_thread::get_id();
            handle.resume();
            --activeResumes_;
            if (activeResumes_ == 0) {
                activeThread_ = {};
                deferredHandle = std::exchange(deferredHandle_, nullptr);
                condition_.notify_all();
            }
        }

        if (deferredHandle) {
            deferredHandle.destroy();
        }
        return true;
    }

    // 返回 false 表示当前恢复栈正在使用该 frame，销毁已延迟到恢复返回后。
    bool destroy(std::coroutine_handle<> handle) noexcept {
        std::unique_lock lock(mutex_);
        destroyed_ = true;
        if (activeResumes_ == 0) {
            return true;
        }
        if (activeThread_ == std::this_thread::get_id()) {
            deferredHandle_ = handle;
            return false;
        }
        condition_.wait(lock, [this] { return activeResumes_ == 0; });
        return true;
    }

 private:
    std::recursive_mutex mutex_;
    std::condition_variable_any condition_;
    bool destroyed_{false};
    uint32_t activeResumes_{0};
    std::thread::id activeThread_;
    std::coroutine_handle<> deferredHandle_;
};

class ResumeGate final {
 public:
    enum class State : uint8_t {
        Pending,
        Armed,
        Signaled,
        Resuming,
        Disarmed,
    };

    ResumeGate(std::coroutine_handle<> handle,
               std::shared_ptr<CoroutineFrameControl> frameControl) noexcept
        : handle_(handle), frameControl_(std::move(frameControl)) {}

    ResumeGate(const ResumeGate&) = delete;
    ResumeGate& operator=(const ResumeGate&) = delete;

    bool arm() noexcept {
        std::lock_guard lock(mutex_);
        if (state_ == State::Pending) {
            state_ = State::Armed;
            return true;
        }
        return false;
    }

    void signal() noexcept {
        std::coroutine_handle<> handle;
        std::shared_ptr<CoroutineFrameControl> frameControl;
        {
            std::lock_guard lock(mutex_);
            if (state_ == State::Pending) {
                state_ = State::Signaled;
                return;
            }
            if (state_ != State::Armed) {
                return;
            }
            state_ = State::Resuming;
            handle = handle_;
            frameControl = frameControl_;
        }

        if (frameControl) {
            frameControl->resume(handle);
        }
    }

    void disarm() noexcept {
        std::lock_guard lock(mutex_);
        if (state_ == State::Pending || state_ == State::Armed) {
            state_ = State::Disarmed;
        }
    }

 private:
    mutable std::mutex mutex_;
    std::coroutine_handle<> handle_;
    std::shared_ptr<CoroutineFrameControl> frameControl_;
    State state_{State::Pending};
};

template <typename Promise>
    requires requires(Promise& promise) { promise.frameControl(); }
std::shared_ptr<ResumeGate> makeResumeGate(std::coroutine_handle<Promise> handle) noexcept {
    return std::make_shared<ResumeGate>(handle, handle.promise().frameControl());
}

}  // namespace slark
