
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "FileUtil.h"
#include "Log.hpp"

#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSPathUtilities.h>
#import <Foundation/NSString.h>
#import <os/log.h>

namespace slark {

void printLog(LogType type, const std::string& log) {
    @autoreleasepool {
        NSString* str = [NSString stringWithUTF8String:log.data()];
        os_log_type_t logType = OS_LOG_TYPE_DEFAULT;
        switch (type) {
            case LogType::Error:   logType = OS_LOG_TYPE_ERROR;   break;
            case LogType::Warning: logType = OS_LOG_TYPE_FAULT;   break;
            case LogType::Info:    logType = OS_LOG_TYPE_INFO;    break;
            case LogType::Debug:   logType = OS_LOG_TYPE_DEBUG;   break;
            case LogType::Print:
            case LogType::Record:
            default:               logType = OS_LOG_TYPE_DEFAULT; break;
        }
        os_log_with_type(OS_LOG_DEFAULT, logType, "%{public}s", log.data());
        str = nil;
    }
}
} // namespace slark

NSString* stringViewToNSString(std::string_view view) {
    return [[NSString alloc] initWithBytes:view.data()
                                    length:view.size()
                                  encoding:NSUTF8StringEncoding];
}

namespace slark::File {

std::string rootPath() noexcept {
    static auto res = [] {
        NSString* homeDir = NSHomeDirectory();
        return std::string([homeDir UTF8String]);
        homeDir = nil;
    }();
    return res;
}

std::string tmpPath() noexcept {
    static auto res = [] {
        NSString* tmpDir = NSTemporaryDirectory();
        return std::string([tmpDir UTF8String]);
        tmpDir = nil;
    }();
    return res;
}

std::string cachePath() noexcept {
    static auto res = [] {
        NSArray* paths =
            NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
        NSString* cachesDir = [paths objectAtIndex:0];
        auto path = std::string([cachesDir UTF8String]);
        cachesDir = nil;
        paths = nil;
        return path;
    }();
    return res;
}

} // namespace slark::File
