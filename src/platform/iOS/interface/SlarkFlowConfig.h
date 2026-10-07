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

@interface SlarkDecoderFlowConfig : NSObject
@property(nonatomic, assign) int32_t lowWatermarkFrames;
@property(nonatomic, assign) int32_t highWatermarkFrames;
@end

@interface SlarkBufferingFlowConfig : NSObject
@property(nonatomic, assign) double startBufferSec;
@property(nonatomic, assign) double rebufferScale;
@property(nonatomic, assign) int32_t starvationThreshold;
@end

@interface SlarkCatchUpFlowConfig : NSObject
@property(nonatomic, assign) int32_t maxDropPerVsync;
@property(nonatomic, assign) int64_t seekFastForwardMs;
@end

@interface SlarkFlowConfig : NSObject
@property(nonatomic, strong) SlarkSourceFlowConfig* source;
@property(nonatomic, strong) SlarkDecoderFlowConfig* audioDecoder;
@property(nonatomic, strong) SlarkDecoderFlowConfig* videoDecoder;
@property(nonatomic, strong) SlarkBufferingFlowConfig* buffering;
@property(nonatomic, strong) SlarkCatchUpFlowConfig* catchUp;
@end

NS_ASSUME_NONNULL_END
