//
// Created by Nevermore on 2023/7/1.
// slark LogOutput
// Copyright (c) 2023 Nevermore All rights reserved.
//
#include "LogOutput.h"
#include "Time.hpp"
#include "Log.hpp"
#include <string_view>

namespace slark {

std::string LogFileName() {
    using namespace slark::FileUtil;
    const static std::string logDir = "logs";
    if (!isDirExist(logDir) && !createDir(logDir)) {
        LogE("create log folder failed.");
        return "";
    }
    return logDir + "/" + Time::localShortTime() + ".log";
}

LogOutput::LogOutput()
    : worker_("LogThread", &LogOutput::writeLog, this) {

}

LogOutput::~LogOutput() = default;

void LogOutput::write(const std::string &str) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        logData_.push_back(str);
    }
    worker_.resume();
}

void LogOutput::updateFile() noexcept {
    file_ = std::make_unique<FileUtil::WriteFile>(LogFileName());
    file_->open();
}

void LogOutput::writeLog() noexcept {
    decltype(logData_) logData;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        logData_.swap(logData);
    }
    if (logData.empty()) {
        worker_.pause();
    }
    std::for_each(logData.begin(), logData.end(), [&](const std::string& str){
        if (!file_ || !file_->isOpen()) {
            updateFile();
        }
        file_->write(str);
        writeCount++;
        if (writeCount >= kMaxWriteCount) {
            updateFile();
        }
    });
}
} // slark
