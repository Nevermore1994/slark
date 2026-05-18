
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "iOSEGLContext.h"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>

@interface EGLContext : NSObject

@end

@implementation EGLContext {
    EAGLContext* _glContext;
}

- (BOOL)initWithContext:(EAGLContext*)context {
    _glContext = nil;
    if (context) {
        _glContext = [[EAGLContext alloc] initWithAPI:context.API sharegroup:context.sharegroup];
    } else {
        _glContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
        if (!_glContext) {
            _glContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
        }
    }
    return _glContext != nil;
}

- (void)dealloc {
    [self releaseResource];
}

- (void)releaseResource {
    [EAGLContext setCurrentContext:nil];
    _glContext = nil;
}

- (EAGLContext*)setCurrentContext:(EAGLContext*)context {
    EAGLContext* preContext = [EAGLContext currentContext];
    if (preContext != context) {
        [EAGLContext setCurrentContext:context];
        return preContext;
    }
    return nil;
}

- (EAGLContext*)activeContext {
    return [self setCurrentContext:_glContext];
}

- (EAGLContext*)context {
    return _glContext;
}

@end

namespace slark {

iOSEGLContext::iOSEGLContext() {
    @autoreleasepool {
        context_ = const_cast<void*>(CFBridgingRetain([EGLContext new]));
    }
}

iOSEGLContext::~iOSEGLContext() {
    @autoreleasepool {
        release();
        CFBridgingRelease(context_);
        context_ = nullptr;
    }
}

bool iOSEGLContext::init(void* context) {
    @autoreleasepool {
        if (inited_) {
            return true;
        }
        inited_ = [(__bridge EGLContext*)context_ initWithContext:(__bridge EAGLContext*)context];
        if (!inited_) {
            release();
        }
        return inited_;
    }
}

void iOSEGLContext::attachContext() {
    @autoreleasepool {
        [(__bridge EGLContext*)context_ activeContext];
    }
}

void iOSEGLContext::detachContext() {
    @autoreleasepool {
        [(__bridge EGLContext*)context_ setCurrentContext:nil];
    }
}

void* iOSEGLContext::nativeContext() {
    @autoreleasepool {
        return (__bridge void*)[(__bridge EGLContext*)context_ context];
    }
}

void iOSEGLContext::release() {
    @autoreleasepool {
        return [(__bridge EGLContext*)context_ releaseResource];
    }
}

IEGLContextRefPtr createGLContext() noexcept {
    return std::make_shared<iOSEGLContext>();
}

} // namespace slark
