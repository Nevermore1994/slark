//
//  SafeDeque.hpp
//  base
//
#pragma once
#include <deque>
#include <mutex>
#include <memory>
#include "NonCopyable.h"
namespace slark {

template<typename T>
class SafeDeque : public NonCopyable {
public:
    SafeDeque() = default;
    ~SafeDeque() override = default;

    inline void push(T value) noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        deque_.push_back(std::move(value));
    }
    
    inline void push(std::vector<T>& valueList) noexcept {
        if (!valueList.empty()) {
            std::unique_lock<std::mutex> lock(mutex_);
            for (auto& value : valueList) {
                deque_.push_back(std::move(value));
            }
        }
        valueList.clear();
    }
    
    [[nodiscard]] inline T pop() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        if (deque_.empty()) {
            return nullptr;
        }
        auto value = std::move(deque_.front());
        deque_.pop_front();
        return value;
    }
    
    inline void swap(SafeDeque& deque) noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        deque_.swap(deque.deque_);
    }
    
    inline std::vector<T> detachData() noexcept {
        std::vector<T> valueList;
        decltype(deque_) tempDeque;
        if (!empty()){
            std::unique_lock<std::mutex> lock(mutex_);
            tempDeque.swap(deque_);
        }
        while (!tempDeque.empty()) {
            valueList.push_back(std::move(tempDeque.front()));
            tempDeque.pop_front();
        }
        return valueList;
    }
    
    inline void clear() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        deque_.clear();
    }

    [[nodiscard]] inline bool empty() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        return deque_.empty();
    }
private:
    std::mutex mutex_;
    std::deque<T> deque_;
};

}
