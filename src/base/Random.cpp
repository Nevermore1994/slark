//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "Random.hpp"

#ifdef __linux__
#include <pthread.h>
#endif

namespace slark::Random {

using namespace std::literals;
constexpr static std::string_view kRandomString =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890"sv;

std::string randomString(uint32_t length) noexcept {
    std::string result;
    result.resize(length);

    auto& gen = detail::engine();
    std::uniform_int_distribution<> dis(0, 61);
    for (uint32_t i = 0; i < length; i++) {
        auto index = static_cast<uint32_t>(dis(gen));
        result[i] = kRandomString[index];
    }
    return result;
}

uint32_t u32Random() noexcept {
    return Random::random<uint32_t>(0, UINT32_MAX);
}

int32_t i32Random() noexcept {
    return Random::random<int32_t>(0, INT32_MAX);
}

std::string uniqueId() noexcept {
    return randomString(64);
}

std::string uuid() noexcept {
    constexpr std::string_view kHexChars = "0123456789abcdef";
    std::string result;
    result.reserve(36);

    auto& gen = detail::engine();
    std::uniform_int_distribution<> dis(0, 15);

    auto appendHex = [&](size_t length) {
        for (size_t i = 0; i < length; ++i) {
            result.push_back(kHexChars[static_cast<size_t>(dis(gen))]);
        }
    };

    appendHex(8);
    result.push_back('-');
    appendHex(4);
    result.push_back('-');

    // UUID v4: version bits = 0100
    result.push_back('4');
    appendHex(3);
    result.push_back('-');

    constexpr std::string_view kVariantChars = "89ab";
    std::uniform_int_distribution<> variantDis(0, 3);
    result.push_back(kVariantChars[static_cast<size_t>(variantDis(gen))]);
    appendHex(3);
    result.push_back('-');

    appendHex(12);
    return result;
}

} // end namespace slark::Random
