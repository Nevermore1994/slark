//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "interface/SlarkFlowConfig.h"

#include "FlowConfig.h"

@implementation SlarkSourceFlowConfig

- (instancetype)init {
    if (self = [super init]) {
        const slark::SourceBackpressureConfig config;
        _lowWatermarkSec = config.lowWatermarkSec;
        _highWatermarkSec = config.highWatermarkSec;
    }
    return self;
}

@end

@implementation SlarkDecoderFlowConfig

- (instancetype)init {
    if (self = [super init]) {
        const slark::DecoderBackpressureConfig config;
        _lowWatermarkFrames = config.lowWatermarkFrames;
        _highWatermarkFrames = config.highWatermarkFrames;
    }
    return self;
}

@end

@implementation SlarkBufferingFlowConfig

- (instancetype)init {
    if (self = [super init]) {
        const slark::BufferingConfig config;
        _startBufferSec = config.startBufferSec;
        _rebufferScale = config.rebufferScale;
        _starvationThreshold = config.starvationThreshold;
    }
    return self;
}

@end

@implementation SlarkCatchUpFlowConfig

- (instancetype)init {
    if (self = [super init]) {
        const slark::CatchUpConfig config;
        _maxDropPerVsync = config.maxDropPerVsync;
        _seekFastForwardMs = config.seekFastForwardMs;
    }
    return self;
}

@end

@implementation SlarkFlowConfig

- (instancetype)init {
    if (self = [super init]) {
        const slark::FlowConfig defaults;
        _source = [SlarkSourceFlowConfig new];
        _audioDecoder = [SlarkDecoderFlowConfig new];
        _audioDecoder.lowWatermarkFrames = defaults.audioDecoder.lowWatermarkFrames;
        _audioDecoder.highWatermarkFrames = defaults.audioDecoder.highWatermarkFrames;
        _videoDecoder = [SlarkDecoderFlowConfig new];
        _buffering = [SlarkBufferingFlowConfig new];
        _catchUp = [SlarkCatchUpFlowConfig new];
    }
    return self;
}

@end
