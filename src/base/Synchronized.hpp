//
//  Synchronized.hpp
//  base
//
//  Copyright (c) 2024 Nevermore All rights reserved.

#pragma once

#include <mutex>
#include <shared_mutex>
#include <functional>
#include <concepts>
#include <chrono>
#include <cassert>
#include <condition_variable>
#include <utility>
#include <cassert>

namespace slark {
template<typename Mutex>
concept IsMutex = requires(Mutex &&mutex) {
    mutex.lock();
    mutex.unlock();
};

template<typename Mutex>
concept IsSharedMutex = IsMutex<Mutex> && requires(Mutex &&mutex) {
    mutex.lock_shared();
    mutex.unlock_shared();
};

template<typename Mutex, typename Rep, typename Period>
concept IsTimeForMutex = IsMutex<Mutex> && requires(Mutex &&mutex,
                                                    const std::chrono::duration<Rep, Period> &duration) {
    mutex.try_lock_for(duration);
    mutex.unlock();
};

template<typename T>
concept IsClock = requires(T &&t){
    typename T::rep;
    typename T::period;
    typename T::duration;
    typename T::time_point;
    t.is_steady;
    t.now();
};

template<typename Mutex, typename Clock, typename Duration>
concept IsTimeUntilMutex = IsMutex<Mutex> && IsClock<Clock> && requires(Mutex &&mutex,
                                                                        const std::chrono::time_point<Clock, Duration> &timePoint) {
    mutex.try_lock_until(timePoint);
    mutex.unlock();
};

template<typename Container, typename Mutex = std::mutex>
class Synchronized: public NonCopyable {
public:
    ~Synchronized() override = default;

    template<typename F>
    auto withLock(F&& func)
    requires IsMutex<Mutex> {
        std::lock_guard<Mutex> lock(mutex_);
        if constexpr (std::is_void_v<std::invoke_result_t<F, Container&>>) {
            func(container_);
            return;
        } else {
            return func(container_);
        }
    }

    template<typename F>
    auto withWriteLock(F&& func)
    requires IsSharedMutex<Mutex> {
        std::unique_lock<Mutex> lock(mutex_);
        if constexpr (std::is_void_v<std::invoke_result_t<F, Container&>>) {
            func(container_);
            return;
        } else {
            return func(container_);
        }
    }

    template<typename F>
    auto withReadLock(F&& func) const
    requires IsSharedMutex<Mutex> {
        std::shared_lock<Mutex> lock(mutex_);
        if constexpr (std::is_void_v<std::invoke_result_t<F, const Container&>>) {
            func(std::as_const(container_));
            return;
        } else {
            return func(std::as_const(container_));
        }
    }

    template<class Rep, class Period, typename F, typename G>
    auto withTryLockFor(const std::chrono::duration<Rep, Period>& duration,
                        F&& lockFunc, G&& failedFunc)
                        requires IsTimeForMutex<Mutex, Rep, Period> {
        if (mutex_.try_lock_for(duration)) {
            std::lock_guard<Mutex> lock(mutex_, std::adopt_lock);
            if constexpr (std::is_void_v<std::invoke_result_t<F, Container&>>) {
                lockFunc(container_);
                return;
            } else {
                return std::optional{lockFunc(container_)};
            }
        } else {
            if constexpr (std::is_void_v<std::invoke_result_t<G>>) {
                failedFunc();
                return;
            } else {
                return std::optional<std::invoke_result_t<F, Container&>>{};
            }
        }
    }

    template<class Clock, class Duration, typename F, typename G>
    auto withTryLockUntil(const std::chrono::time_point<Clock, Duration>& timePoint,
                          F&& lockFunc, G&& failedFunc)
                          requires IsTimeUntilMutex<Mutex, Clock, Duration> {
        if (mutex_.try_lock_until(timePoint)) {
            std::lock_guard<Mutex> lock(mutex_, std::adopt_lock);
            if constexpr (std::is_void_v<std::invoke_result_t<F, Container&>>) {
                lockFunc(container_);
                return;
            } else {
                return std::optional{lockFunc(container_)};
            }
        } else {
            if constexpr (std::is_void_v<std::invoke_result_t<G>>) {
                failedFunc();
                return;
            } else {
                return std::optional<std::invoke_result_t<F, Container&>>{};
            }
        }
    }

    template<typename F>
    auto withScopeLock(Synchronized& synchronized, F&& func) {
        auto lock1 = std::unique_lock<Mutex>{mutex_, std::defer_lock};
        auto lock2 = std::unique_lock<Mutex>{synchronized.mutex_, std::defer_lock};
        std::lock(lock1, lock2);
        if constexpr (std::is_void_v<std::invoke_result_t<F, Container&, Container&>>) {
            func(container_, synchronized.container_);
            return;
        } else {
            return func(container_, synchronized.container_);
        }
    }

    void wait(std::condition_variable &cond) const {
        std::unique_lock<Mutex> lock(mutex_);
        cond.wait(lock);
    }

    void wait(std::condition_variable &cond, std::function<bool(void)> &&func) {
        assert(func != nullptr);
        std::unique_lock<Mutex> lock(mutex_);
        cond.wait(lock, func);
    }

private:
    Container container_;
    mutable Mutex mutex_;
};

///Only the thread safety of the pointer is guaranteed, not the safety of the object itself.
///
template<typename T>
class AtomicWeakPtr: public NonCopyable {
public:
    AtomicWeakPtr() = default;
    ~AtomicWeakPtr() override = default;
    explicit AtomicWeakPtr(std::weak_ptr<T> ptr) : ptr_(std::move(ptr)) {}

    std::weak_ptr<T> swap(std::weak_ptr<T> rhs) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto p = ptr_;
        ptr_ = rhs;
        return p;
    }

    void store(std::weak_ptr<T> rhs) {
        std::lock_guard<std::mutex> lock(mutex_);
        ptr_ = rhs;
    }

    std::shared_ptr<T> load() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ptr_.lock();
    }

private:
    std::weak_ptr<T> ptr_;
    mutable std::mutex mutex_;
};

///Only the thread safety of the pointer is guaranteed, not the safety of the object itself.
///A replacement for std::atomic<std::shared_ptr<T>>
template<typename T>
class AtomicSharedPtr: public NonCopyable {
public:
    AtomicSharedPtr() = default;
    ~AtomicSharedPtr() override = default;

    explicit AtomicSharedPtr(std::shared_ptr<T> ptr) : ptr_(std::move(ptr)) {}

    void reset(std::shared_ptr<T> ptr = nullptr) {
        std::atomic_store(&ptr_, ptr);
    }

    std::shared_ptr<T> swap(std::shared_ptr<T> rhs) {
        return std::atomic_exchange(&ptr_, rhs);
    }

    std::shared_ptr<T> load() const {
        return std::atomic_load(&ptr_);
    }

private:
    std::shared_ptr<T> ptr_;
    mutable std::mutex mutex_;
};

}
