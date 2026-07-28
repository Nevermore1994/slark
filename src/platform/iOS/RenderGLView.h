//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#import <UIKit/UIKit.h>

#import "IRenderView.h"

namespace slark {
struct IVideoRender;
}

@interface RenderGLView : UIView <IRenderView>
@property(atomic, assign, readonly) BOOL isActive;
@property(nonatomic, assign) double rotation;
@property(nonatomic, assign, readonly) BOOL supportsHDR;

- (instancetype)initWithFrame:(CGRect)frame;
- (void)start;
- (void)stop;
- (void*)renderImpl;
@end
