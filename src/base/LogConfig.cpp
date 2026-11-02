//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "LogConfig.h"

#include "Log.hpp"

namespace slark {

namespace internal {
constinit std::atomic<int> gMinLogLevel{kMinLogLevelDefault};
}  // namespace internal

static LogConfig gLogConfig;

void setLogConfig(const LogConfig& config) {
    gLogConfig = config;
    internal::gMinLogLevel.store(config.minLogLevel, std::memory_order_relaxed);
}

const LogConfig& getLogConfig() {
    return gLogConfig;
}

}  // namespace slark
