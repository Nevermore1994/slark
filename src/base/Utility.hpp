//
//  Utility.hpp
//  slark
//
//  Created by Nevermore on 2023/10/20.
//  Copyright (c) 2023 Nevermore All rights reserved.

#pragma once

#include <memory>
#include <cstdint>
#include <string>
#include <expected>

namespace slark::Util {

template <typename T>
std::expected<uint64_t, bool> getWeakPtrAddress(const std::weak_ptr<T>& ptr) noexcept {
    if (ptr.expired()) {
        return std::unexpected(false);
    }
    return reinterpret_cast<uint64_t>(ptr.lock().get());
}

std::string genRandomName(const std::string& namePrefix) noexcept;

}


