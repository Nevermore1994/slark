//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface SlarkSourceFlowConfig : NSObject
@property(nonatomic, assign) double lowWatermarkSec;
@property(nonatomic, assign) double highWatermarkSec;
@end

@interface SlarkBufferingFlowConfig : NSObject
@property(nonatomic, assign) double startBufferSec;
@property(nonatomic, assign) double rebufferEnterSec;
@property(nonatomic, assign) double rebufferExitSec;
@end

@interface SlarkDnsFlowConfig : NSObject
@property(nonatomic, copy) NSArray<NSString*>* preResolveUrls;
@property(nonatomic, assign) double cacheTtlSec;
@end

@interface SlarkNotifyFlowConfig : NSObject
@property(nonatomic, assign) double progressStepSec;
@property(nonatomic, assign) double cacheIntervalSec;
@end

@interface SlarkFlowConfig : NSObject
@property(nonatomic, strong) SlarkSourceFlowConfig* source;
@property(nonatomic, strong) SlarkBufferingFlowConfig* buffering;
@property(nonatomic, strong) SlarkDnsFlowConfig* dns;
@property(nonatomic, strong) SlarkNotifyFlowConfig* notify;
@end

NS_ASSUME_NONNULL_END
