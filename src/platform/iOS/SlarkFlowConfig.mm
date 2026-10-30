//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "interface/SlarkFlowConfig.h"

#include "FlowConfig.h"

@implementation SlarkSourceFlowConfig

- (instancetype)init {
    if (self = [super init]) {
        const slark::SourceWatermarkConfig config;
        _lowWatermarkSec = config.lowWatermarkSec;
        _highWatermarkSec = config.highWatermarkSec;
    }
    return self;
}

@end

@implementation SlarkBufferingFlowConfig

- (instancetype)init {
    if (self = [super init]) {
        const slark::BufferingConfig config;
        _startBufferSec = config.startBufferSec;
        _rebufferEnterSec = config.rebufferEnterSec;
        _rebufferExitSec = config.rebufferExitSec;
    }
    return self;
}

@end

@implementation SlarkFlowConfig

- (instancetype)init {
    if (self = [super init]) {
        _source = [SlarkSourceFlowConfig new];
        _buffering = [SlarkBufferingFlowConfig new];
    }
    return self;
}

@end
