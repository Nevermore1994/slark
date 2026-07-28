//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "CancellationToken.h"
#include "CoroutineScheduler.hpp"
#include "Log.hpp"
#include "NonCopyable.h"
#include "Task.hpp"
#include "ThreadPool.hpp"
#include "Type.h"

namespace slark::http::detail {

inline std::thread::id currentThreadId() noexcept {
    return std::this_thread::get_id();
}

void dispatchResponse(HttpResponse& response,
                      const RequestInfo& requestInfo,
                      HttpResponseHandler& handler);

// 桥接状态机：收敛所有状态转换
enum class BridgeState : uint8_t {
    Pending,    // 初始状态，等待启动
    Running,    // 协程正在执行
    Completed,  // 协程正常完成
    Cancelled,  // 被取消
    Failed,     // 执行失败
};

class ICallbackBridge : public NonCopyable {
 public:
    virtual ~ICallbackBridge() = default;
    virtual void cancel() noexcept = 0;
    [[nodiscard]] virtual CancellationToken cancellationToken() const noexcept = 0;
};

template <typename LaunchFunc>
class CallbackBridge final : public ICallbackBridge,
                             public std::enable_shared_from_this<CallbackBridge<LaunchFunc>> {
 public:
    explicit CallbackBridge(LaunchFunc launcher, HttpResponseHandler handler)
        : launcher_(std::move(launcher)), handler_(std::move(handler)), token_() {}

    ~CallbackBridge() override {
        cancel();
        waitForExit();
    }

    void start(RequestInfo requestInfo) {
        requestInfo_ = std::move(requestInfo);
        auto weak = this->weak_from_this();
        bridgePool().postJob([weak]() {
            auto self = weak.lock();
            if (self) {
                self->run();
            }
        });
    }

    void cancel() noexcept override {
        auto expected = BridgeState::Pending;
        if (state_.compare_exchange_strong(expected, BridgeState::Cancelled)) {
            token_.cancel();
            if (handler_.onError) {
                handler_.onError(requestInfo_, ErrorInfo{slark::Error::Cancelled});
            }
            monitorExited_.store(true, std::memory_order_release);
            exitCv_.notify_all();
            return;
        }
        expected = BridgeState::Running;
        if (state_.compare_exchange_strong(expected, BridgeState::Cancelled)) {
            token_.cancel();
        }
    }

    [[nodiscard]] CancellationToken cancellationToken() const noexcept override {
        return token_;
    }

 private:
    void run() {
        auto expected = BridgeState::Pending;
        if (!state_.compare_exchange_strong(expected, BridgeState::Running)) {
            state_.store(BridgeState::Cancelled, std::memory_order_release);
            monitorExited_.store(true, std::memory_order_release);
            exitCv_.notify_all();
            return;
        }

        if (token_.isCancelled()) {
            state_.store(BridgeState::Cancelled, std::memory_order_release);
            monitorExited_.store(true, std::memory_order_release);
            exitCv_.notify_all();
            return;
        }

        requestCoroutine_ = launcher_(requestInfo_, token_);
        monitorCoroutine_ = monitorTask();
        monitorCoroutine_.getHandle().resume();
    }

    Task<void> monitorTask() {
        monitorThreadId_ = currentThreadId();
        auto weak = this->weak_from_this();
        ErrorInfo finalError{slark::Error::Success};

        try {
            auto response = co_await std::move(requestCoroutine_);

            auto self = weak.lock();
            if (!self) {
                co_return;
            }
            auto expected = BridgeState::Running;
            if (state_.compare_exchange_strong(expected, BridgeState::Completed)) {
                dispatchResponse(response, requestInfo_, handler_);
            } else {
                auto currentState = state_.load(std::memory_order_acquire);
                if (currentState == BridgeState::Cancelled) {
                    finalError = ErrorInfo{slark::Error::Cancelled};
                } else {
                    finalError = ErrorInfo{slark::Error::Failed};
                }
            }
        } catch (...) {
            auto self = weak.lock();
            if (!self) {
                co_return;
            }
            auto expected = BridgeState::Running;
            state_.compare_exchange_strong(expected, BridgeState::Failed);
            finalError = ErrorInfo{slark::Error::Failed};
        }

        auto self = weak.lock();
        if (!self) {
            co_return;
        }
        auto finalState = state_.load(std::memory_order_acquire);
        if (finalState == BridgeState::Cancelled || finalState == BridgeState::Failed) {
            if (handler_.onError) {
                handler_.onError(requestInfo_, finalError);
            }
        }

        monitorExited_.store(true, std::memory_order_release);
        exitCv_.notify_all();
    }

    void waitForExit() noexcept {
        if (currentThreadId() == monitorThreadId_) {
            return;
        }

        std::unique_lock<std::mutex> lock(exitMutex_);
        constexpr auto kMaxWaitMs = std::chrono::milliseconds(5000);
        exitCv_.wait_for(lock, kMaxWaitMs, [this] {
            auto s = state_.load(std::memory_order_acquire);
            return (s != BridgeState::Pending && s != BridgeState::Running) &&
                   monitorExited_.load(std::memory_order_acquire);
        });
        auto expected = BridgeState::Running;
        state_.compare_exchange_strong(expected, BridgeState::Cancelled);
    }

    static ThreadPool& bridgePool() {
        static ThreadPool pool(ThreadPoolConfig{2});
        return pool;
    }

    LaunchFunc launcher_;
    HttpResponseHandler handler_;
    RequestInfo requestInfo_;
    CancellationToken token_;
    Task<HttpResponse> requestCoroutine_;
    Task<void> monitorCoroutine_;
    std::atomic<BridgeState> state_{BridgeState::Pending};
    std::atomic<bool> monitorExited_{false};
    std::thread::id monitorThreadId_{};
    std::mutex exitMutex_;
    std::condition_variable exitCv_;
};

template <typename LaunchFunc>
std::shared_ptr<ICallbackBridge> startCallbackBridge(LaunchFunc&& launcher,
                                                     HttpResponseHandler handler,
                                                     RequestInfo requestInfo) {
    using BridgeType = CallbackBridge<std::decay_t<LaunchFunc>>;
    auto bridge =
        std::make_shared<BridgeType>(std::forward<LaunchFunc>(launcher), std::move(handler));
    bridge->start(std::move(requestInfo));
    return bridge;
}

}  // namespace slark::http::detail