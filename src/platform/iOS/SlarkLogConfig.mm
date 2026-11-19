//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "interface/SlarkLogConfig.h"

#import "Log.hpp"
#import "LogConfig.h"

static_assert(SlarkLogLevelPrint == static_cast<NSInteger>(slark::LogType::Print),
              "SlarkLogLevel must mirror slark::LogType");
static_assert(SlarkLogLevelDebug == static_cast<NSInteger>(slark::LogType::Debug),
              "SlarkLogLevel must mirror slark::LogType");
static_assert(SlarkLogLevelInfo == static_cast<NSInteger>(slark::LogType::Info),
              "SlarkLogLevel must mirror slark::LogType");
static_assert(SlarkLogLevelWarning == static_cast<NSInteger>(slark::LogType::Warning),
              "SlarkLogLevel must mirror slark::LogType");
static_assert(SlarkLogLevelError == static_cast<NSInteger>(slark::LogType::Error),
              "SlarkLogLevel must mirror slark::LogType");
static_assert(SlarkLogLevelAssert == static_cast<NSInteger>(slark::LogType::Assert),
              "SlarkLogLevel must mirror slark::LogType");
static_assert(SlarkLogLevelRecord == static_cast<NSInteger>(slark::LogType::Record),
              "SlarkLogLevel must mirror slark::LogType");

@implementation SlarkLogConfig

- (instancetype)init {
    if (self = [super init]) {
        const slark::LogConfig defaults;
        _progressLogBytes = defaults.progressLogBytes;
        _maxLogFileSize = defaults.maxLogFileSize;
        _maxLogFileCount = defaults.maxLogFileCount;
        _minLogLevel = static_cast<SlarkLogLevel>(slark::kMinLogLevelDefault);
    }
    return self;
}

+ (void)setLogConfig:(SlarkLogConfig*)config {
    if (!config) {
        return;
    }

    slark::LogConfig cppConfig;
    cppConfig.progressLogBytes = config.progressLogBytes;
    cppConfig.maxLogFileSize = config.maxLogFileSize;
    cppConfig.maxLogFileCount = config.maxLogFileCount;
    cppConfig.minLogLevel = static_cast<int>(config.minLogLevel);
    if (config.logDir) {
        cppConfig.logDir = [config.logDir UTF8String];
    }
    if (config.logFileName) {
        cppConfig.logFileName = [config.logFileName UTF8String];
    }
    slark::setLogConfig(cppConfig);
}

@end
