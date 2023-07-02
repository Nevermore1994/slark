//
// Created by Nevermore on 2021/10/24.
// slark log
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include "Log.hpp"
#include "Time.hpp"
#include "LogOutput.h"
#include <cstdarg>

#ifdef SLARK_IOS
    #include "InternalLog.hpp"
#endif

namespace slark {

void PrintLog(LogType level, const char* format, ...) {
    std::string logStr = Time::LocalTime();
    logStr.append(kLogStrs[static_cast<size_t>(level)]);
    char buf[kMaxLogBuffSize] = {0};
    va_list args;
    va_start(args, format);
    vsnprintf(reinterpret_cast<char*>(buf), (kMaxLogBuffSize - 1), format, args);
    va_end(args);
    buf[kMaxLogBuffSize - 1] = 0; //last element = 0

    logStr.append(buf);
    LogOutput::shareInstance().write(logStr);
#ifdef SLARK_IOS
    outputLog(std::move(logStr));
#elif SLARK_BASE
#endif
    fprintf(stdout, "%s\n", logStr.c_str());
    fflush(stdout);
}

}//end namespace slark