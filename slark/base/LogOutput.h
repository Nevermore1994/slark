//
// Created by Nevermore on 2023/7/1.
// slark LogOutput
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once

#include <mutex>
#include <vector>
#include "Thread.h"
#include "File.h"

namespace slark {

constexpr uint32_t kMaxWriteCount = 10240;

class LogOutput {
public:
    inline static LogOutput& shareInstance() {
        static LogOutput instance_;
        return instance_;
    }

    LogOutput();
    ~LogOutput();
    void write(const std::string& str) noexcept;
private:
    void updateFile() noexcept;
    void writeLog() noexcept;
private:
    Thread worker_;
    std::vector<std::string> logData_;
    std::mutex mutex_;
    std::unique_ptr<FileUtil::WriteFile> file_;
    uint32_t writeCount = 0;
};

} // slark