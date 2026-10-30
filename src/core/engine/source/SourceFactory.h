//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <functional>
#include <memory>

#include "Command.h"
#include "SeekEpoch.h"
#include "Source.h"

namespace slark::engine {

// Returns null for a scheme that no source can serve.
class SourceFactory {
 public:
    using Wake = std::function<void()>;

    [[nodiscard]] static std::unique_ptr<Source> create(const Prepare& prepare,
                                                        SeekEpoch epoch,
                                                        Wake wake);
};

}  // namespace slark::engine
