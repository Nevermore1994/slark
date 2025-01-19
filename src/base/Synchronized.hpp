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

template <typename Mutex>
concept IsMutex = requires (Mutex&& mutex) {
    mutex.lock();
    mutex.unlock();
};

template <typename Mutex>
concept IsSharedMutex = IsMutex<Mutex> && requires (Mutex&& mutex) {
    mutex.lock_shared();
    mutex.unlock_shared();
};

template <typename Mutex, typename Rep, typename Period>
concept IsTimeForMutex = IsMutex<Mutex> && requires (Mutex&& mutex, const std::chrono::duration<Rep, Period>& duration) {
    mutex.try_lock_for(duration);
    mutex.unlock();
};

template<typename T>
concept IsClock = requires (T&& t){
    typename T::rep;
    typename T::period;
    typename T::duration;
    typename T::time_point;
    t.is_steady;
    t.now();
};

template <typename Mutex, typename Clock, typename Duration>
concept IsTimeUntilMutex = IsMutex<Mutex> && IsClock<Clock> && requires (Mutex&& mutex, const std::chrono::time_point<Clock, Duration>& timePoint) {
    mutex.try_lock_until(timePoint);
    mutex.unlock();
};

template <typename Container, typename Mutex = std::mutex>
class Synchronized {
public:
    void withLock(std::function<void(Container&)> lockFunc)
    requires IsMutex<Mutex> {
        assert(lockFunc != nullptr);
        std::lock_guard<Mutex> lock(mutex_);
        if (lockFunc) lockFunc(container_);
    }

    void withWriteLock(std::function<void(Container&)> lockFunc)
    requires IsSharedMutex<Mutex>  {
        assert(lockFunc != nullptr);
        std::unique_lock<Mutex> lock(mutex_);
        if (lockFunc) lockFunc(container_);
    }

    void withReadLock(std::function<void(const Container&)> lockFunc)
    requires IsSharedMutex<Mutex> {
        assert(lockFunc != nullptr);
        std::shared_lock<Mutex> lock(mutex_);
        if (lockFunc) lockFunc(std::as_const(container_));
    }

    template <class Rep, class Period>
    void withTryLockFor(const std::chrono::duration<Rep, Period>& duration, std::function<void(Container&)>&& lockFunc, std::function<void(void)>&& failedFunc)
    requires IsTimeForMutex <Mutex, Rep, Period> {
        assert(lockFunc != nullptr);
        assert(failedFunc != nullptr);
        if (mutex_.try_lock_for(duration)) {
            if (lockFunc) lockFunc(container_);
        } else {
            if (failedFunc) failedFunc();
        }
    }

    template <class Clock, class Duration>
    void withTryLockUntil(const std::chrono::time_point<Clock, Duration>& timePoint, std::function<void(Container&)>&& lockFunc, std::function<void(void)>&& failedFunc)
    requires IsTimeUntilMutex<Mutex, Clock, Duration> {
        assert(lockFunc != nullptr);
        assert(failedFunc != nullptr);
        if (mutex_.try_lock_until(timePoint)) {
            if (lockFunc) lockFunc(container_);
        } else {
            if (failedFunc) failedFunc();
        }
    }
    
    void wait(std::condition_variable& cond) {
        std::unique_lock<Mutex> lock(mutex_);
        cond.wait(lock);
    }

    void wait(std::condition_variable& cond, std::function<bool(void)>&& func) {
        assert(func != nullptr);
        std::unique_lock<Mutex> lock(mutex_);
        cond.wait(lock, func);
    }

    void withScopeLock(Synchronized& synchronized, std::function<void(Container&, Container&)>&& func) {
        auto lock1 = std::unique_lock<Mutex> {mutex_, std::defer_lock};
        auto lock2 = std::unique_lock<Mutex> {synchronized.mutex_, std::defer_lock};
        std::lock(lock1, lock2);
        if (func) func(container_, synchronized.container_);
    }
    
private:
    Container container_;
    Mutex mutex_;
};
