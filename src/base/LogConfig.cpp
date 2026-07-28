//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "LogConfig.h"

namespace slark {

static LogConfig gLogConfig;

void setLogConfig(const LogConfig& config) {
    gLogConfig = config;
}

const LogConfig& getLogConfig() {
    return gLogConfig;
}

}  // namespace slark
