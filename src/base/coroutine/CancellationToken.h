//
// Copyright (c) 2026 Nevermore-. All rights reserved.
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
                return;  // already cancelled
            }
            for (auto& [id, cb] : state_->callbackMap) {
                if (cb) {
                    callbacks.push_back(std::move(cb));
                }
            }
            state_->callbackMap.clear();
        }
        // Run callbacks outside the lock to avoid deadlock
        for (const auto& cb : callbacks) {
            if (cb) {
                cb();
            }
        }
    }

    [[nodiscard]] bool isCancelled() const noexcept {
        return state_->cancelled.load(std::memory_order_acquire);
    }

    // Register a cancel callback, returns a registration handle (invalid when already cancelled)
    [[nodiscard]] CancellationTokenRegistration registerCallback(Callback callback) const;

    // Registers the callback, or runs it immediately when the token is already cancelled.
    // This closes the check/register window in one place: a cancel that lands between
    // "checked isCancelled()" and "registered the callback" would otherwise be lost.
    [[nodiscard]] CancellationTokenRegistration registerCallbackOrRun(Callback callback) const;

    // Unregister a specific callback
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

    [[nodiscard]] static CancellationToken link(const CancellationToken& a,
                                                const CancellationToken& b);

    // Cancelled whenever the parent is; cancelling it never cancels the parent. Null means no parent.
    [[nodiscard]] static CancellationToken childOf(const CancellationToken* parent);

    // Equal identities mean the same shared state, that is the same token.
    [[nodiscard]] const void* identity() const noexcept {
        return state_.get();
    }

 private:
    friend class CancellationTokenRegistration;
    friend struct CancellationTokenTestAccess;

    struct State {
        std::atomic<bool> cancelled{false};
        std::mutex mutex;
        std::unordered_map<TokenId, Callback> callbackMap;
        TokenId nextTokenId{0};
        // Keeps the sources alive while the forwarding registrations below still point at them
        std::vector<std::shared_ptr<State>> linkSources;
        // Forward-callback registrations created by follow(), kept alive with the State
        std::vector<std::shared_ptr<CancellationTokenRegistration>> linkedRegs;
    };

    explicit CancellationToken(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}

    // Only for a token nobody else holds yet: a cycle of followers could never be released.
    void follow(const CancellationToken& source);

    std::shared_ptr<State> state_;
};

// Callback registration handle, used to unregister the callback
class CancellationTokenRegistration {
 public:
    using TokenId = CancellationToken::TokenId;

    CancellationTokenRegistration() noexcept = default;
    CancellationTokenRegistration(CancellationTokenRegistration&& other) noexcept;
    CancellationTokenRegistration& operator=(CancellationTokenRegistration&& other) noexcept;
    ~CancellationTokenRegistration();

    CancellationTokenRegistration(const CancellationTokenRegistration&) = delete;
    CancellationTokenRegistration& operator=(const CancellationTokenRegistration&) = delete;

    // Unregister the callback
    void unregister() noexcept;

    // Check whether the handle is still valid
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

inline CancellationTokenRegistration CancellationToken::registerCallback(Callback callback) const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->cancelled.load(std::memory_order_acquire)) {
        return CancellationTokenRegistration();
    }
    auto tokenId = ++state_->nextTokenId;
    state_->callbackMap[tokenId] = std::move(callback);
    return CancellationTokenRegistration(state_, tokenId);
}

inline CancellationTokenRegistration CancellationToken::registerCallbackOrRun(
    Callback callback) const {
    Callback toRunNow;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (!state_->cancelled.load(std::memory_order_acquire)) {
            auto tokenId = ++state_->nextTokenId;
            state_->callbackMap[tokenId] = std::move(callback);
            return CancellationTokenRegistration(state_, tokenId);
        }
        toRunNow = std::move(callback);
    }
    if (toRunNow) {
        toRunNow();
    }
    return CancellationTokenRegistration();
}

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

inline void CancellationToken::follow(const CancellationToken& source) {
    state_->linkSources.push_back(source.state_);
    auto registration = std::make_shared<CancellationTokenRegistration>(
        source.registerCallbackOrRun([weak = std::weak_ptr<State>(state_)]() {
            if (auto state = weak.lock()) {
                CancellationToken(std::move(state)).cancel();
            }
        }));
    if (registration->isValid()) {
        state_->linkedRegs.push_back(std::move(registration));
    }
}

inline CancellationToken CancellationToken::link(const CancellationToken& a,
                                                 const CancellationToken& b) {
    CancellationToken linked;
    if (a.isCancelled() || b.isCancelled()) {
        linked.cancel();
        return linked;
    }
    linked.follow(a);
    linked.follow(b);
    return linked;
}

inline CancellationToken CancellationToken::childOf(const CancellationToken* parent) {
    CancellationToken child;
    if (parent != nullptr) {
        child.follow(*parent);
    }
    return child;
}

}  // namespace slark
