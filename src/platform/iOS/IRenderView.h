//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <memory>

namespace slark {

struct IVideoRender;
using IVideoRenderWeakPtr = std::weak_ptr<IVideoRender>;

} // namespace slark

#import <UIKit/UIKit.h>

/**
 * Protocol for all render views (OpenGL ES / Metal).
 */
@protocol IRenderView <NSObject>

@property (atomic, assign, readonly) BOOL isActive;
@property (nonatomic, assign) double rotation;
@property (nonatomic, assign, readonly) BOOL supportsHDR;

- (void)start;
- (void)stop;
/// Returns a heap-allocated weak_ptr<IVideoRender>; caller takes ownership.
- (void*)renderImpl;

@end
