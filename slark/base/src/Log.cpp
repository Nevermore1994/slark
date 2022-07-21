//
// Created by Nevermore on 2021/10/24.
// slark log
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include "Log.hpp"
#include "date.h"
#include <cstdarg>

//todo:platform log
using namespace slark;

void slark::printLog(LogType level, const char *format, ...) {
    std::string logStr = date::format("%F %T", std::chrono::system_clock::now());
    logStr.append(kLogStrs[static_cast<int>(level)]);
    
    char buf[kMaxLogBuffSize] = {0};
    va_list args;
    va_start(args, format);
    vsnprintf((char *) (buf), (kMaxLogBuffSize - 1), format, args);
    va_end(args);
    buf[kMaxLogBuffSize - 1] = 0; //last element = 0
    
    logStr.append(buf);
    fprintf(stdout, "%s\n", logStr.c_str());
    fflush(stdout);
}

