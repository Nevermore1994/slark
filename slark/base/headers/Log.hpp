//
// Created by Nevermore on 2021/10/24.
// Slark log
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <cstdio>
#include <string>

namespace Slark {

enum class LogType {
    Debug = 0,
    Info,
    Warning,
    Error,
    Assert
};

constexpr const char* kLogStrs[] = {" [debug] ", " [info] ", " [warning] ", " [error] ", " [assert] "};

constexpr const uint16_t kMaxLogBuffSize = 1024;

void printLog(LogType level, const char* format, ...);

#define logger(level, format, args...) \
    do {                               \
        printLog(level, "[%s][Line:%d][Function:%s]" format, __FILE__, __LINE__, __FUNCTION__, ##args);  \
    } while(0)

#define  LogD(format, args...)  logger(LogType::Debug, format, ##args)
#define  LogI(format, args...)  logger(LogType::Info, format, ##args)
#define  LogW(format, args...)  logger(LogType::Warning, format, ##args)
#define  LogE(format, args...)  logger(LogType::Error, format, ##args)

}


