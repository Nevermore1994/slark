//
// Created by Nevermore on 2026/4/2.
// Slark CancellationToken - Cooperative cancellation for coroutines
// Copyright (c) 2026 Nevermore All rights reserved.
//
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace slark {

// 前向声明
class CancellationTokenRegistration;

class CancellationToken {
public:
    using Callback = std::function<void()>;
    using TokenId = uint64_t;

    CancellationToken() : state_(std::make_shared<State>()) {}

    void cancel() noexcept {
        std::vector<Callback> callbacks;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->cancelled.exchange(true, std::memory_order_acq_rel)) {
                return; // 已经取消
            }
            // 从 callbackMap 中提取回调执行
            for (auto& [id, cb] : state_->callbackMap) {
                if (cb) {
                    callbacks.push_back(std::move(cb));
                }
            }
            state_->callbackMap.clear();
        }
        // 在锁外执行回调，避免死锁
        for (const auto& cb : callbacks) {
            if (cb) {
                cb();
            }
        }
    }

    [[nodiscard]] bool isCancelled() const noexcept {
        return state_->cancelled.load(std::memory_order_acquire);
    }

    // 注册取消回调，返回注册句柄（已取消时返回无效句柄）
    [[nodiscard]] CancellationTokenRegistration registerCallback(Callback callback) const;

    // 取消注册特定回调
    bool unregisterCallback(TokenId tokenId) const noexcept {
        if (tokenId == 0) {
            return false;
        }
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->callbackMap.erase(tokenId) > 0;
    }

    explicit operator bool() const noexcept {
        return !isCancelled();
    }

private:
    friend class CancellationTokenRegistration;

    struct State {
        std::atomic<bool> cancelled{false};
        std::mutex mutex;
        std::unordered_map<TokenId, Callback> callbackMap;
        TokenId nextTokenId{0};
    };

    std::shared_ptr<State> state_;
};

// 回调注册句柄，可用于注销回调
class CancellationTokenRegistration {
public:
    using TokenId = CancellationToken::TokenId;

    CancellationTokenRegistration() noexcept = default;
    CancellationTokenRegistration(CancellationTokenRegistration&& other) noexcept;
    CancellationTokenRegistration& operator=(CancellationTokenRegistration&& other) noexcept;
    ~CancellationTokenRegistration();

    // 禁用拷贝
    CancellationTokenRegistration(const CancellationTokenRegistration&) = delete;
    CancellationTokenRegistration& operator=(const CancellationTokenRegistration&) = delete;

    // 注销回调
    void unregister() noexcept;

    // 检查是否有效
    [[nodiscard]] bool isValid() const noexcept {
        return tokenId_ != 0 && !weakState_.expired();
    }

private:
    friend class CancellationToken;

    CancellationTokenRegistration(std::weak_ptr<CancellationToken::State> state,
                                  TokenId tokenId) noexcept
        : weakState_(std::move(state)), tokenId_(tokenId) {}

    std::weak_ptr<CancellationToken::State> weakState_;
    TokenId tokenId_{0};
};

// CancellationToken 内联实现
inline CancellationTokenRegistration CancellationToken::registerCallback(Callback callback) const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->cancelled.load(std::memory_order_acquire)) {
        return CancellationTokenRegistration();
    }
    auto tokenId = ++state_->nextTokenId;
    state_->callbackMap[tokenId] = std::move(callback);
    return CancellationTokenRegistration(state_, tokenId);
}

// CancellationTokenRegistration 内联实现
inline CancellationTokenRegistration::CancellationTokenRegistration(
    CancellationTokenRegistration&& other) noexcept
    : weakState_(std::move(other.weakState_)), tokenId_(other.tokenId_) {
    other.tokenId_ = 0;
}

inline CancellationTokenRegistration& CancellationTokenRegistration::operator=(
    CancellationTokenRegistration&& other) noexcept {
    if (this != &other) {
        unregister();
        weakState_ = std::move(other.weakState_);
        tokenId_ = other.tokenId_;
        other.tokenId_ = 0;
    }
    return *this;
}

inline CancellationTokenRegistration::~CancellationTokenRegistration() {
    unregister();
}

inline void CancellationTokenRegistration::unregister() noexcept {
    if (tokenId_ == 0) {
        return;
    }
    auto state = weakState_.lock();
    if (!state) {
        tokenId_ = 0;
        return;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    state->callbackMap.erase(tokenId_);
    tokenId_ = 0;
}

} // namespace slark