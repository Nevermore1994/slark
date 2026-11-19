//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.api

data class SlarkLogConfig(
    // Progress log throttle: emit a progress line only after this many bytes accumulate
    val progressLogBytes: Long = SlarkConfigDefaults.progressLogBytes,

    // Max single log file size (bytes); rotates to the next file when exceeded
    val maxLogFileSize: Long = SlarkConfigDefaults.maxLogFileSize,

    // Max number of retained log files; oldest files are removed when exceeded
    val maxLogFileCount: Int = SlarkConfigDefaults.maxLogFileCount,

    // Minimum log level that is printed and written to file
    val minLogLevel: SlarkLogLevel = SlarkLogLevel.DEFAULT,

    // Log output directory; empty uses the default path
    val logDir: String = "",

    // Log file name prefix; actual file name is <prefix>_<time>.log, auto-generated when empty
    val logFileName: String = ""
)
