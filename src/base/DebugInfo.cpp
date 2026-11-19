//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DebugInfo.h"

#include <array>
#include <utility>

namespace slark {
namespace {

constexpr std::array<std::pair<const char*, std::string DebugInfo::*>, 10> kGroups = {{
    {"Source", &DebugInfo::source},
    {"Video", &DebugInfo::video},
    {"Audio", &DebugInfo::audio},
    {"Playback", &DebugInfo::playback},
    {"Sync", &DebugInfo::sync},
    {"Buffer", &DebugInfo::buffer},
    {"Queue", &DebugInfo::queue},
    {"Render", &DebugInfo::render},
    {"Network", &DebugInfo::network},
    {"System", &DebugInfo::system},
}};

}  // namespace

std::string formatDebugInfo(const DebugInfo& info) {
    std::string out;
    for (const auto& [title, field] : kGroups) {
        out.push_back('[');
        out.append(title);
        out.append("]\n");
        const auto& block = info.*field;
        if (block.empty()) {
            out.append("  n/a\n");
        } else {
            out.append(block);
        }
    }
    return out;
}

}  // namespace slark
