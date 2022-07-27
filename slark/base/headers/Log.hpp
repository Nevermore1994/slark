//
// Created by Nevermore on 2021/10/24.
// slark log
// Copyright (c) 2021 Nevermore All rights reserved.
//
#pragma once

#include <cstdio>
#include <string>

namespace slark {

enum class LogType {
    Debug = 0,
    Info,
    Warning,
    Error,
    Assert
};

constexpr const char *kLogStrs[] = {" [debug] ", " [info] ", " [warning] ", " [error] ", " [assert] "};

constexpr const uint16_t kMaxLogBuffSize = 1024;

void printLog(LogType level, const char *format, ...);

#define logger(level, format, args...) \
    do {                               \
        std::string formatStr("[%s][Line:%d][Function:%s] "); \
        formatStr.append(format);                               \
        printLog(level, formatStr.c_str(), __FILE__, __LINE__, __FUNCTION__, ##args);  \
    } while(0)

#define  logd(format, args...)  logger(LogType::Debug, format, ##args)
#define  logi(format, args...)  logger(LogType::Info, format, ##args)
#define  logw(format, args...)  logger(LogType::Warning, format, ##args)
#define  loge(format, args...)  logger(LogType::Error, format, ##args)
#define  loga(format, args...)  logger(LogType::Assert, format, ##args)

}


