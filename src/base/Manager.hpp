//
// Created by Nevermore on 2025/4/26.
//
#pragma once
#include <unordered_map>
#include <memory>
#include <mutex>
#include "NonCopyable.h"

namespace slark {

template <typename T>
class Manager: public NonCopyable{
public:
    using RefPtr = std::shared_ptr<T>;
    static Manager& shareInstance() noexcept {
        static auto instance_ = std::unique_ptr<Manager>(new Manager<T>());
        return *instance_;
    }

    ~Manager() override = default;
public:
    RefPtr find(const std::string& itemId) noexcept {
        std::lock_guard lock(mutex_);
        if (manager_.contains(itemId)) {
            return manager_.at(itemId);
        }
        return nullptr;
    }

    void add(const std::string& itemId, RefPtr ptr) noexcept {
        std::lock_guard lock(mutex_);
        manager_[itemId] = std::move(ptr);
    }


    void remove(const std::string& itemId) noexcept {
        std::lock_guard lock(mutex_);
        manager_.erase(itemId);
    }
private:
    Manager() = default;

private:
    std::mutex mutex_;
    std::unordered_map<std::string, RefPtr> manager_;
};

}