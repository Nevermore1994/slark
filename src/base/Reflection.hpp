//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "Synchronized.hpp"

namespace slark {

class Reflection {
 public:
    using Factory = std::function<std::shared_ptr<void>()>;

    static Reflection& shareInstance() noexcept {
        static Reflection instance;
        return instance;
    }

    void registerFactory(std::string name, Factory factory) noexcept {
        factories_.withLock([&](auto& map) {
            map.insert_or_assign(std::move(name), std::move(factory));
        });
    }

    template <typename T>
    std::shared_ptr<T> create(const std::string& name) noexcept {
        auto factory = factories_.withLock([&](const auto& map) -> Factory {
            if (auto it = map.find(name); it != map.end()) {
                return it->second;
            }
            return nullptr;
        });
        if (!factory) {
            return nullptr;
        }
        return std::static_pointer_cast<T>(factory());
    }

    template <typename T>
    static std::string enroll(const std::string& name) noexcept {
        shareInstance().registerFactory(name, []() -> std::shared_ptr<void> {
            return std::make_shared<T>();
        });
        return name;
    }

 private:
    Reflection() = default;
    ~Reflection() = default;

    Synchronized<std::unordered_map<std::string, Factory>> factories_;
};

#define SLARK_GET_CLASS_NAME(cls) #cls
#define SLARK_REGISTER_CLASS(cls)                       \
    static inline const std::string cls##_registered_ = \
        ::slark::Reflection::enroll<cls>(SLARK_GET_CLASS_NAME(cls))

}  // namespace slark
