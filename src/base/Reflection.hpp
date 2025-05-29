//
// Created by Nevermore on 2022/5/13.
// Copyright (c) 2022 Nevermore All rights reserved.
//

#pragma once
#include <unordered_map>
#include <string>
#include <functional>
#include <mutex>
#include "NonCopyable.h"

namespace slark {

using ReflectionFunction = std::function<std::shared_ptr<void>(void)>;

class Reflection {
public:
    static Reflection& shareInstance() noexcept {
        static Reflection instance;
        return instance;
    }

    inline ReflectionFunction generate(const std::string& name) noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = builders_.find(name);
        if (it == builders_.end()) {
            return nullptr;
        }
        return it->second;
    }

    inline void enrolment(std::string name, ReflectionFunction func) noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        builders_.insert_or_assign(std::move(name), std::move(func));
    }

private:
    Reflection() = default;

    ~Reflection() = default;

private:
    std::unordered_map<std::string, ReflectionFunction> builders_;
    std::mutex mutex_;
};

template<typename T>
std::string RegisterClass(const std::string& name, std::function<std::shared_ptr<T>()>&& func) noexcept {
    Reflection::shareInstance().enrolment(name, [func = std::move(func)]() {
        return func();
    });
    return name;
}

template<typename T>
inline std::shared_ptr<T> GenerateInstance(const std::string& name) {
    auto f = Reflection::shareInstance().generate(name);
    if (f) {
        auto instance = f();
        if (instance) {
            return std::static_pointer_cast<T>(instance);
        }
    }
    return nullptr;
}

#define GetClassName(name) #name


struct BaseClass {
    template<typename T>
    static std::string registerClass(std::string name) noexcept{
        return RegisterClass<T>(name, []() {
            return std::make_shared<T>();
        });
    }

    template<typename T>
    static std::shared_ptr<T> create(const std::string& name) noexcept {
        return GenerateInstance<T>(name);
    }
};

}
