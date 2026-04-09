//
//  Types.h
//  slark
//
//  Created by Nevermore on 2025/4/3.

#pragma once

#include <concepts>
#include <cstdint>
#include <span>
#include <string_view>

namespace slark {

using Byte = uint8_t;

using ByteSpan = std::span<Byte>;

using ConstByteSpan = std::span<const Byte>;

template <typename T>
concept ByteLike = std::same_as<T, uint8_t> || std::same_as<T, int8_t> || std::same_as<T, char> ||
                   std::same_as<T, unsigned char> || std::same_as<T, std::byte>;

template <typename T>
concept ByteContainer = requires(T t) {
    typename T::value_type;
    requires ByteLike<typename T::value_type>;
    { t.data() } -> std::convertible_to<const Byte*>;
    { t.size() } -> std::convertible_to<std::size_t>;
    { t.empty() } -> std::convertible_to<bool>;
};

template <typename T>
concept ByteRange = std::ranges::range<T> && ByteLike<std::ranges::range_value_t<T>>;

template <typename T>
concept ConvertibleToByteSpan = std::convertible_to<T, ConstByteSpan> ||
                                std::convertible_to<T, std::string_view> || requires(T t) {
                                    { t.data() } -> std::convertible_to<const Byte*>;
                                    { t.size() } -> std::convertible_to<std::size_t>;
                                };

} // namespace slark
