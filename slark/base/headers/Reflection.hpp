//
// Created by Nevermore on 2022/5/13.
// Copyright (c) 2022 Nevermore All rights reserved.
//

#pragma once
#include <unordered_map>
#include <string>
#include <functional>
#include <any>
#include <mutex>
#include "NonCopyable.hpp"

namespace slark{

using ReflectionFunction = std::function<void*(void)>;

class Reflection {
public:
    static Reflection& shareInstance() noexcept {
        static Reflection instance;
        return instance;
    }
    
    inline ReflectionFunction generate(const std::string& name) noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = builders_.find(name);
        if(it == builders_.end()) {
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
void registerClass(const std::string& name) {
    return Reflection::shareInstance().enrolment(name, []() {
        return new T();
    });
}

inline void* generate(const std::string& name) {
    auto f = Reflection::shareInstance().generate(name);
    if(f) {
        return f();
    }
    return nullptr;
}

#define RegisterClass(name) \
    registerClass<name>(#name)

#define GenerateClass(name) \
    generate(name)
}
