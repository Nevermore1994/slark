//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, SlarkLogLevel) {
    SlarkLogLevelPrint = 0,
    SlarkLogLevelDebug = 1,
    SlarkLogLevelInfo = 2,
    SlarkLogLevelWarning = 3,
    SlarkLogLevelError = 4,
    SlarkLogLevelAssert = 5,
    SlarkLogLevelRecord = 6,
};

@interface SlarkLogConfig : NSObject

// Progress log throttle: emit a progress line only after this many bytes accumulate
@property(nonatomic, assign) uint64_t progressLogBytes;

// Max single log file size (bytes); rotates to the next file when exceeded
@property(nonatomic, assign) uint64_t maxLogFileSize;

// Max number of retained log files; oldest files are removed when exceeded
@property(nonatomic, assign) uint32_t maxLogFileCount;

// Minimum log level to write; logs below this level are not written to file
@property(nonatomic, assign) SlarkLogLevel minLogLevel;

// Log output directory; empty uses the default path
@property(nonatomic, copy, nullable) NSString* logDir;

// Log file name prefix; actual file name is <prefix>_<time>.log, auto-generated when empty
@property(nonatomic, copy, nullable) NSString* logFileName;

// Set global log config; call before creating a SlarkPlayer
+ (void)setLogConfig:(SlarkLogConfig*)config;

@end

NS_ASSUME_NONNULL_END
