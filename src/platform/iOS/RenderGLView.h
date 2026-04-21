//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#import "RenderGLViewDelegate.h"

#import <UIKit/UIKit.h>

namespace slark {
class IVideoRender;
}

@interface RenderGLView : UIView <RenderGLViewDelegate>
@property (atomic, assign, readonly) BOOL isActive;
@property (nonatomic, assign) double rotation;

- (instancetype)initWithFrame:(CGRect)frame;
- (void)start;
- (void)stop;
- (std::weak_ptr<slark::IVideoRender>)renderImpl;
@end
