//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <string>
#include <string_view>

namespace slark {

// RFC 3986 5.2.4 remove_dot_segments; preserves empty and trailing segments.
[[nodiscard]] inline std::string removeDotSegments(std::string_view input) {
    std::string output;
    output.reserve(input.size());

    const auto removeLastSegment = [&output] {
        const auto slash = output.rfind('/');
        output.erase(slash == std::string::npos ? 0 : slash);
    };

    while (!input.empty()) {
        if (input.starts_with("../")) {
            input.remove_prefix(3);
        } else if (input.starts_with("./")) {
            input.remove_prefix(2);
        } else if (input == "/.") {
            input = "/";
        } else if (input.starts_with("/./")) {
            input.remove_prefix(2);
        } else if (input == "/..") {
            input = "/";
            removeLastSegment();
        } else if (input.starts_with("/../")) {
            input.remove_prefix(3);
            removeLastSegment();
        } else if (input == "." || input == "..") {
            input = {};
        } else {
            const auto segmentEnd = input.find('/', input.front() == '/' ? 1 : 0);
            if (segmentEnd == std::string_view::npos) {
                output.append(input);
                input = {};
            } else {
                output.append(input.substr(0, segmentEnd));
                input.remove_prefix(segmentEnd);
            }
        }
    }
    return output;
}

}  // namespace slark
