//
// Created by Nevermore on 2021/10/24.
// Slark log
// Copyright (c) 2021 Nevermore All rights reserved.
//
#include "Log.hpp"
#include "Time.hpp"
#include <cstdarg>

#ifdef SLARK_IOS
    #include "InternalLog.hpp"
#endif

namespace Slark {

void printLog(LogType level, const char* format, ...) {
    std::string logStr = Time::localTime();
    logStr.append(kLogStrs[static_cast<int>(level)]);
    char buf[kMaxLogBuffSize] = {0};
    va_list args;
    va_start(args, format);
    vsnprintf(reinterpret_cast<char*>(buf), (kMaxLogBuffSize - 1), format, args);
    va_end(args);
    buf[kMaxLogBuffSize - 1] = 0; //last element = 0

    logStr.append(buf);
#ifdef SLARK_IOS
    outputLog(std::move(logStr));
#else
    fprintf(stdout, "%s\n", logStr.c_str());
    fflush(stdout);
#endif
}

}//end namespace Slark
