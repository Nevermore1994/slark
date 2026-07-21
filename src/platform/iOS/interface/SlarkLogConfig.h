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

// 进度日志限频：单次累计接收/产出超过此字节数才打一次 progress
@property (nonatomic, assign) uint64_t progressLogBytes;

// 单个日志文件最大大小（字节），超出后轮转到下一个文件
@property (nonatomic, assign) uint64_t maxLogFileSize;

// 日志文件最大保留个数，超出后删除最旧文件
@property (nonatomic, assign) uint32_t maxLogFileCount;

// 最低输出日志等级，低于此等级的日志不写文件
@property (nonatomic, assign) SlarkLogLevel minLogLevel;

// 日志输出目录，为空则使用默认路径
@property (nonatomic, copy, nullable) NSString *logDir;

// 日志文件名前缀，实际文件名为 前缀_时间.log，为空则自动生成
@property (nonatomic, copy, nullable) NSString *logFileName;

// 设置全局日志配置，在创建 SlarkPlayer 之前调用
+ (void)setLogConfig:(SlarkLogConfig *)config;

@end

NS_ASSUME_NONNULL_END
