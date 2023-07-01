//
// Created by Nevermore on 2021/11/3.
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

namespace slark {

class NonCopyable {
public:
    NonCopyable() = default;

    virtual ~NonCopyable() = default;

    NonCopyable(const NonCopyable&) = delete;

    NonCopyable& operator=(const NonCopyable&) = delete;

    NonCopyable(NonCopyable&&) = delete;

    NonCopyable& operator=(NonCopyable&&) = delete;
};

}
