//
// Created by Nevermore on 2025/4/26.
//

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
    RefPtr find(const std::string& decoderId) noexcept {
        std::lock_guard lock(mutex_);
        if (manager_.contains(decoderId)) {
            return manager_.at(decoderId);
        }
        return nullptr;
    }

    void add(const std::string& decoderId, RefPtr ptr) noexcept {
        std::lock_guard lock(mutex_);
        manager_[decoderId] = std::move(ptr);
    }


    void remove(const std::string& decoderId) noexcept {
        std::lock_guard lock(mutex_);
        manager_.erase(decoderId);
    }
private:
    Manager() = default;

private:
    std::mutex mutex_;
    std::unordered_map<std::string, RefPtr> manager_;
};

}