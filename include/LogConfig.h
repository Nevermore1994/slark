//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>
#include <string>

namespace slark {

// Log levels, kept in sync with LogType in Log.hpp
enum class LogType : int;

constexpr int kMinLogLevelDefault = 1;  // LogType::Debug

struct LogConfig {
    // Progress log throttle: emit a progress line only after this many bytes accumulate
    uint64_t progressLogBytes = 512 * 1024;

    // Max single log file size (bytes); rotates to the next file when exceeded
    uint64_t maxLogFileSize = 8 * 1024 * 1024;

    // Max number of retained log files; oldest files are removed when exceeded
    uint32_t maxLogFileCount = 10;

    // Minimum log level to write; logs below this level are not written to file.
    // Defaults to Debug, i.e. every level is output.
    int minLogLevel = kMinLogLevelDefault;

    // Log output directory; empty uses the default path
    std::string logDir;

    // Log file name prefix; actual file name is <prefix>_<time>.log, auto-generated when empty
    std::string logFileName;
};

// SDK global log config; set before creating a Player
void setLogConfig(const LogConfig& config);
const LogConfig& getLogConfig();

}  // namespace slark
