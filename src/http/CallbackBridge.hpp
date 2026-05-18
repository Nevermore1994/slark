//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include "CancellationToken.h"
#include "Coroutine.hpp"
#include "CoroutineScheduler.hpp"
#include "Log.hpp"
#include "NonCopyable.h"
#include "ThreadPool.hpp"
#include "Type.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace slark::http::detail {

// 获取当前线程 ID 的辅助函数
inline std::thread::id currentThreadId() noexcept {
    return std::this_thread::get_id();
}

void dispatchResponse(HttpResponse& response,
                      const RequestInfo& requestInfo,
                      HttpResponseHandler& handler);

// 桥接状态机：收敛所有状态转换
enum class BridgeState : uint8_t {
    Pending,   // 初始状态，等待启动
    Running,   // 协程正在执行
    Completed, // 协程正常完成
    Cancelled, // 被取消
    Failed,    // 执行失败
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
        // 合并等待逻辑，减少阻塞时间
        waitForExit();
    }

    void start(RequestInfo requestInfo) {
        requestInfo_ = std::move(requestInfo);
        // 使用 shared_from_this() 自持生命周期，防止 this 悬空
        auto self = this->shared_from_this();
        bridgePool().postJob([self]() {
            self->run();
        });
    }

    void cancel() noexcept override {
        // 尝试从 Pending 取消，监控协程未启动，需要主动派发错误回调
        auto expected = BridgeState::Pending;
        if (state_.compare_exchange_strong(expected, BridgeState::Cancelled)) {
            token_.cancel();
            // 在 Pending 状态取消，监控协程未启动，需要主动派发错误回调
            if (handler_.onError) {
                handler_.onError(requestInfo_, ErrorInfo{ResultCode::Cancelled, 0});
            }
            monitorExited_.store(true, std::memory_order_release);
            return;
        }
        // 尝试从 Running 取消
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
        // 尝试从 Pending 转换到 Running
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

        // 启动请求协程并存储为成员
        requestCoroutine_ = launcher_(requestInfo_, token_);
        // 启动监控协程
        monitorCoroutine_ = monitorTask();
        CoroutineScheduler::instance().schedule(monitorCoroutine_.getHandle());
    }

    // 独立的成员协程函数，co_await 成员变量 requestCoroutine_
    // 持有 shared_from_this 自引用，防止对象析构后协程访问成员变量导致 UAF
    Coroutine<void> monitorTask() {
        // 记录执行线程 ID，用于析构时检测自阻塞
        monitorThreadId_ = currentThreadId();
        // 自引用确保协程执行期间对象生命周期有效
        auto self = this->shared_from_this();
        ErrorInfo finalError{ResultCode::Success, 0};

        try {
            auto response = co_await requestCoroutine_;

            auto expected = BridgeState::Running;
            if (state_.compare_exchange_strong(expected, BridgeState::Completed)) {
                dispatchResponse(response, requestInfo_, handler_);
            } else {
                // 状态非 Running，记录当前状态对应的错误
                auto currentState = state_.load(std::memory_order_acquire);
                if (currentState == BridgeState::Cancelled) {
                    finalError = ErrorInfo{ResultCode::Cancelled, 0};
                } else {
                    finalError = ErrorInfo{ResultCode::Failed, 0};
                }
            }
        } catch (...) {
            auto expected = BridgeState::Running;
            state_.compare_exchange_strong(expected, BridgeState::Failed);
            finalError = ErrorInfo{ResultCode::Failed, 0};
        }

        // 对于取消或失败状态，派发错误回调确保用户收到通知
        auto finalState = state_.load(std::memory_order_acquire);
        if (finalState == BridgeState::Cancelled || finalState == BridgeState::Failed) {
            if (handler_.onError) {
                handler_.onError(requestInfo_, finalError);
            }
        }

        // 标记监控协程已退出并通知等待线程
        monitorExited_.store(true, std::memory_order_release);
        exitCv_.notify_all();
    }

    void waitForExit() noexcept {
        // 自阻塞检测：如果在 monitorTask 线程中析构，跳过等待避免死锁
        // 此时应已处于 monitorTask() 尾部或已完成，无需等待
        if (currentThreadId() == monitorThreadId_) {
            return;
        }

        // 使用条件变量等待，避免轮询 sleep
        std::unique_lock<std::mutex> lock(exitMutex_);
        constexpr auto kMaxWaitMs = std::chrono::milliseconds(5000);
        exitCv_.wait_for(lock, kMaxWaitMs, [this] {
            auto s = state_.load(std::memory_order_acquire);
            return (s != BridgeState::Pending && s != BridgeState::Running) &&
                   monitorExited_.load(std::memory_order_acquire);
        });
        // 超时后强制标记为取消
        auto expected = BridgeState::Running;
        state_.compare_exchange_strong(expected, BridgeState::Cancelled);
    }

    void spinWaitForNonPending() noexcept {
        // 短暂自旋等待，确保状态已离开 Pending
        constexpr auto kMaxSpinIterations = 10000;
        for (int32_t i = 0; i < kMaxSpinIterations; ++i) {
            if (state_.load(std::memory_order_acquire) != BridgeState::Pending) {
                return;
            }
            std::this_thread::yield();
        }
        // 超时后强制标记为取消
        auto expected = BridgeState::Pending;
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
    Coroutine<HttpResponse> requestCoroutine_{nullptr};
    Coroutine<void> monitorCoroutine_{nullptr};
    std::atomic<BridgeState> state_{BridgeState::Pending};
    std::atomic<bool> monitorExited_{false};
    std::thread::id monitorThreadId_{}; // 记录 monitorTask 执行线程，用于检测自阻塞
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

} // namespace slark::http::detail