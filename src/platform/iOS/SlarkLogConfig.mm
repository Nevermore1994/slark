//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "interface/SlarkLogConfig.h"
#import "LogConfig.h"
#import "Log.hpp"

@implementation SlarkLogConfig

- (instancetype)init {
    if (self = [super init]) {
        _progressLogBytes = 512 * 1024;
        _maxLogFileSize = 8 * 1024 * 1024;
        _maxLogFileCount = 10;
        _minLogLevel = SlarkLogLevelDebug;
    }
    return self;
}

+ (void)setLogConfig:(SlarkLogConfig *)config {
    if (!config) return;

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
