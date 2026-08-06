//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.api

data class SlarkLogConfig(
    // Progress log throttle: emit a progress line only after this many bytes accumulate
    val progressLogBytes: Long = 512 * 1024,

    // Max single log file size (bytes); rotates to the next file when exceeded
    val maxLogFileSize: Long = 8 * 1024 * 1024,

    // Max number of retained log files; oldest files are removed when exceeded
    val maxLogFileCount: Int = 10,

    // Minimum log level to write; logs below this level are not written to file
    val minLogLevel: SlarkLogLevel = SlarkLogLevel.DEBUG,

    // Log output directory; empty uses the default path
    val logDir: String = "",

    // Log file name prefix; actual file name is <prefix>_<time>.log, auto-generated when empty
    val logFileName: String = ""
)

enum class SlarkLogLevel(val value: Int) {
    PRINT(0),
    DEBUG(1),
    INFO(2),
    WARNING(3),
    ERROR(4),
    ASSERT(5),
    RECORD(6)
}
