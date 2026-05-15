//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once
#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace slark {

struct StringUtil {
    static std::vector<std::string_view> split(std::string_view stringView,
                                               std::string_view delimiter) noexcept;

    static std::string removeSpace(std::string_view view) noexcept;

    [[nodiscard]] static bool startsWith(std::string_view str, std::string_view prefix) noexcept {
        return str.starts_with(prefix);
    }

    [[nodiscard]] static bool endsWith(std::string_view str, std::string_view suffix) noexcept {
        return str.ends_with(suffix);
    }

    [[nodiscard]] static bool contains(std::string_view str, std::string_view sub) noexcept {
        return str.find(sub) != std::string_view::npos;
    }

    [[nodiscard]] static std::string toLower(std::string str) noexcept {
        std::ranges::transform(str, str.begin(), [](unsigned char c) {
            return std::tolower(c);
        });
        return str;
    }

    [[nodiscard]] static bool iequals(std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) {
            return false;
        }
        return std::ranges::equal(a, b, [](unsigned char ca, unsigned char cb) {
            return std::tolower(ca) == std::tolower(cb);
        });
    }

    template <typename T>
    [[nodiscard]] static std::optional<T> fromChars(std::string_view sv, int base = 10) noexcept {
        if constexpr (std::is_floating_point_v<T>) {
            char buf[64];
            if (sv.size() >= sizeof(buf)) {
                return std::nullopt;
            }
            std::memcpy(buf, sv.data(), sv.size());
            buf[sv.size()] = '\0';
            char* end = nullptr;
            T value;
            if constexpr (std::is_same_v<T, float>) {
                value = std::strtof(buf, &end);
            } else {
                value = static_cast<T>(std::strtod(buf, &end));
            }
            if (end == buf) {
                return std::nullopt;
            }
            return value;
        } else {
            T value{};
            auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value, base);
            if (ec != std::errc()) {
                return std::nullopt;
            }
            return value;
        }
    }

    template <typename T>
    [[nodiscard]] static std::optional<T> fromChars(std::string_view sv,
                                                    size_t pos,
                                                    size_t count,
                                                    int base = 10) noexcept {
        if (pos >= sv.size()) {
            return std::nullopt;
        }
        return fromChars<T>(sv.substr(pos, count), base);
    }
};

} // namespace slark
