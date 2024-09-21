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

using ReflectionFunction = std::function<void* (void)>;

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
std::string RegisterClass(const std::string& name) {
    Reflection::shareInstance().enrolment(name, []() {
        return new (std::nothrow) T();
    });
    return name;
}

inline void* GenerateInstance(const std::string& name) {
    auto f = Reflection::shareInstance().generate(name);
    if (f) {
        return f();
    }
    return nullptr;
}

#define GetClassName(name) #name

template<typename T>
struct BaseClass {
    inline static std::string registerClass(std::string name) {
        return RegisterClass<T>(name);
    }

    inline static T* create(const std::string& name) noexcept {
        return reinterpret_cast<T*>(GenerateInstance(name));
    }
};

}
