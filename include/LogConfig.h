//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>
#include <string>

namespace slark {

// 日志等级，与 Log.hpp 中 LogType 保持同步
enum class LogType : int;

constexpr int kMinLogLevelDefault = 1; // LogType::Debug

struct LogConfig {
    // 进度日志限频：单次累计接收/产出超过此字节数才打一次 progress
    uint64_t progressLogBytes = 512 * 1024;

    // 单个日志文件最大大小（字节），超出后轮转到下一个文件
    uint64_t maxLogFileSize = 8 * 1024 * 1024;

    // 日志文件最大保留个数，超出后删除最旧文件
    uint32_t maxLogFileCount = 10;

    // 最低输出日志等级，低于此等级的日志不写文件
    // 默认为 Debug，即所有等级都输出
    int minLogLevel = kMinLogLevelDefault;

    // 日志输出目录，为空则使用默认路径
    std::string logDir;

    // 日志文件名前缀，实际文件名为 前缀_时间.log，为空则自动生成
    std::string logFileName;
};

// SDK 全局日志配置，在创建 Player 之前设置
void setLogConfig(const LogConfig& config);
const LogConfig& getLogConfig();

} // namespace slark
