//
// Created by Nevermore on 2022/5/17.
// Slark BaseClass
// Copyright (c) 2022 Nevermore All rights reserved.
//

#pragma once

#include <string>
#include <memory>
#include "Reflection.hpp"

namespace Slark {

#define GetClassName(name) #name

#define HAS_MEMBER(XXX) \
    template<typename T, typename... Args>\
    struct has_member_##XXX \
    { \
    private:  \
        template<typename U> static auto Check(int) -> decltype(std::declval<U>().XXX(std::declval<Args>()...), std::true_type());  \
        template<typename U> static std::false_type Check(...); \
    public: \
        static constexpr auto value = decltype(Check<T>(0))::value; \
    }

HAS_MEMBER(name);

template<typename T>
struct BaseClass {
    inline static std::string registerClass() {
        Slark::RegisterClass(T);
        return GetClassName(T);
    }

    inline static T* create(std::string name) noexcept {
        return reinterpret_cast<T*>(Slark::GenerateClass(name));
    }
};


}
