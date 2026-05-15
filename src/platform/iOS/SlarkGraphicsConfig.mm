//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "interface/SlarkGraphicsConfig.h"

#import <UIKit/UIKit.h>

@implementation SlarkGraphicsConfig

+ (instancetype)defaultConfig {
    return [self configWithBackend:SlarkGraphicsBackendMetal];
}

+ (instancetype)configWithBackend:(SlarkGraphicsBackend)backend {
    SlarkGraphicsConfig* config = [SlarkGraphicsConfig new];
    config.backend = backend;
    config.hdrEnabled = NO;
    return config;
}

- (SlarkGraphicsBackend)effectiveBackend {
    return _hdrEnabled ? SlarkGraphicsBackendMetal : _backend;
}

+ (BOOL)isHDRDisplayAvailable {
    if (@available(iOS 13.0, *)) {
        return [UIScreen mainScreen].currentEDRHeadroom > 1.0f;
    }
    return NO;
}

@end
