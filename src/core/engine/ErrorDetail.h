//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <cstdint>
#include <string>

namespace slark::engine {

struct ErrorDetail {
    int32_t origin = 0;
    std::string host;
};

}  // namespace slark::engine
