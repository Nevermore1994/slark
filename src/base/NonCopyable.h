//
// Created by Nevermore on 2023/11/3.
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

namespace slark {

class NonCopyable {
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;

public:
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;

    NonCopyable(NonCopyable&&) = default;
    NonCopyable& operator=(NonCopyable&&) = default;
};

class NonCopyMovable {
protected:
    NonCopyMovable() = default;
    ~NonCopyMovable() = default;

public:
    NonCopyMovable(const NonCopyMovable&) = delete;
    NonCopyMovable& operator=(const NonCopyMovable&) = delete;

    NonCopyMovable(NonCopyMovable&&) = delete;
    NonCopyMovable& operator=(NonCopyMovable&&) = delete;
};

} // namespace slark
