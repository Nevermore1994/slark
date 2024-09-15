//
// Created by Nevermore on 2023/7/1.
// slark LogOutput
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include "LogOutput.h"
#include "Time.hpp"
#include "Log.hpp"
#include <memory>

namespace slark {

std::string LogFileName() {
    using namespace slark::FileUtil;
    const std::string logDir = cachePath() + "/logs";
    if (!isDirExist(logDir) && !createDir(logDir)) {
        LogP("create log folder failed.");
        return "";
    }
    return logDir + "/" + Time::localShortTime() + ".log";
}

constexpr uint32_t kMaxWriteLogCount = 8 * 1024;

LogOutput::LogOutput() = default;

LogOutput::~LogOutput() = default;

void LogOutput::write(const std::string& str) noexcept {
    writer_.withLock([&](auto& writer){
        if (writer == nullptr) {
            updateFile(writer);
        }
        writer->write(str);
        if (writer->writeCount() >= kMaxWriteLogCount) {
            updateFile(writer);
        }
    });
}

void LogOutput::updateFile(std::unique_ptr<Writer>& writer) noexcept {
    auto path = LogFileName();
    if (path.empty()) {
        LogP("log file path is empty");
        return;
    }
    writer = std::make_unique<Writer>(path, "LogOutput");
}

} // slark
