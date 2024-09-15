//
// Created by Nevermore on 2023/7/1.
// slark LogOutput
// Copyright (c) 2023 Nevermore All rights reserved.
//
#pragma once
#include "Writer.hpp"

namespace slark {

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
    static void updateFile(std::unique_ptr<Writer>& writer) noexcept;
private:
    Synchronized<std::unique_ptr<Writer>> writer_;
};

} // slark
