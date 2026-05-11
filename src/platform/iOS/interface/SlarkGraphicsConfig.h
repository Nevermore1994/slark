//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#import <Foundation/Foundation.h>

typedef NS_ENUM(NSUInteger, SlarkGraphicsBackend) {
    SlarkGraphicsBackendOpenGLES,
    SlarkGraphicsBackendMetal,
};

/**
 * Graphics configuration passed to SlarkPlayer / SlarkViewController.
 * Mirrors Android GraphicsConfig.
 *
 * HDR content requires Metal backend; if hdrEnabled is YES the backend
 * will be forced to Metal regardless of the `backend` property.
 */
@interface SlarkGraphicsConfig : NSObject

@property (nonatomic, assign) SlarkGraphicsBackend backend;
@property (nonatomic, assign) BOOL hdrEnabled;

+ (instancetype)defaultConfig;
+ (instancetype)configWithBackend:(SlarkGraphicsBackend)backend;

/// Returns the effective backend, taking HDR override into account.
- (SlarkGraphicsBackend)effectiveBackend;

+ (BOOL)isHDRDisplayAvailable;

@end
