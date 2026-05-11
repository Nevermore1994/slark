//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#import "IRenderView.h"

#import <UIKit/UIKit.h>

/**
 * Metal-backed render view.
 * Uses CAMetalLayer + CVMetalTextureCache for zero-copy YUV rendering.
 * Supports HDR output via RGBA16Float pixel format when hdrEnabled is YES.
 */
@interface RenderMetalView : UIView <IRenderView>

- (instancetype)initWithFrame:(CGRect)frame;
- (instancetype)initWithFrame:(CGRect)frame hdrEnabled:(BOOL)hdrEnabled;

/// YES when initialized with hdrEnabled:YES and the device supports EDR.
@property (nonatomic, assign, readonly) BOOL supportsHDR;

@end
