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
    using namespace slark::File;
    const std::string logDir = cachePath() + "/logs";
    if (!isDirExist(logDir) && !createDir(logDir)) {
        LogP("create log folder failed.");
        return "";
    }
    return logDir + "/" + Time::localShortTimeStr() + ".log";
}

constexpr uint32_t kMaxWriteLogCount = 80 * 1024;

LogOutput& LogOutput::shareInstance(){
   static std::unique_ptr<LogOutput> instance_;
   static std::once_flag flag;
   std::call_once(flag, []{
       instance_ = std::make_unique<LogOutput>();
   });
   return *instance_;
}

LogOutput::LogOutput() {
    writer_.withLock([&](auto& writer){
        writer = std::make_unique<Writer>();
        recreateFile(writer);
    });
}

LogOutput::~LogOutput() = default;

void LogOutput::write(const std::string& str) noexcept {
    writer_.withLock([&](auto& writer){
        writer->write(str);
        if (writer->writeCount() >= kMaxWriteLogCount) {
            recreateFile(writer);
        }
    });
}

void LogOutput::recreateFile(std::unique_ptr<Writer>& writer) noexcept {
    auto path = LogFileName();
    if (path.empty()) {
        LogP("log file path is empty");
        return;
    }
    writer->open(path);
}

} // slark
