//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.api

data class SlarkLogConfig(
    // 进度日志限频：单次累计接收/产出超过此字节数才打一次 progress
    val progressLogBytes: Long = 512 * 1024,

    // 单个日志文件最大大小（字节），超出后轮转到下一个文件
    val maxLogFileSize: Long = 8 * 1024 * 1024,

    // 日志文件最大保留个数，超出后删除最旧文件
    val maxLogFileCount: Int = 10,

    // 最低输出日志等级，低于此等级的日志不写文件
    val minLogLevel: SlarkLogLevel = SlarkLogLevel.DEBUG,

    // 日志输出目录，为空则使用默认路径
    val logDir: String = "",

    // 日志文件名前缀，实际文件名为 前缀_时间.log，为空则自动生成
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
