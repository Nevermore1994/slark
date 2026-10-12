//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#import <Foundation/Foundation.h>

typedef NS_ENUM(NSUInteger, SlarkGraphicsBackend) {
    SlarkGraphicsBackendOpenGLES,
    SlarkGraphicsBackendMetal,
};

@interface SlarkGraphicsConfig : NSObject

@property(nonatomic, assign) SlarkGraphicsBackend backend;
@property(nonatomic, assign) BOOL hdrEnabled;

+ (instancetype)defaultConfig;
+ (instancetype)configWithBackend:(SlarkGraphicsBackend)backend;

+ (BOOL)isHDRDisplayAvailable;

@end
