//
//  iOSEGLContext.cpp
//  slark
//
//  Created by Nevermore on 2024/10/30.
//


#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#include "iOSEGLContext.h"

#define ReleaseRef(x) if (x) { CFRelease(x); x = NULL; }

@interface EGLContext : NSObject

@end

@implementation EGLContext
{
    uint32_t _width;
    uint32_t _height;
    EAGLContext* _glContext;
    GLuint _frameBuffer;
    CVOpenGLESTextureRef _texture;
    CVOpenGLESTextureCacheRef _textureCache;
    CVPixelBufferRef _pixelBuffer;
    CVPixelBufferPoolRef _pixelBufferPool;
}

- (BOOL)initWithContext:(EAGLContext*) context {
    _width = 0;
    _height = 0;
    _frameBuffer = 0;
    _texture = nil;
    _textureCache = nil;
    _pixelBuffer = nil;
    _pixelBufferPool = nil;
    _glContext = nil;
    if (context) {
        _glContext = [[EAGLContext alloc] initWithAPI:context.API sharegroup:context.sharegroup];
    } else {
        if (!context) {
            _glContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
        }
        if (!context) {
            _glContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
        }
    }
    return _glContext != nil;
}

- (void)dealloc {
    [self releaseResource];
}

- (void)releaseResource {
    EAGLContext* preContext = [self activeContext];
    if (_frameBuffer > 0) {
        glDeleteFramebuffers(1, &_frameBuffer);
        _frameBuffer = 0;
    }
    if (_pixelBuffer) {
        CVPixelBufferRelease(_pixelBuffer);
        _pixelBuffer = nil;
    }
    if (_pixelBufferPool) {
        CVPixelBufferPoolRelease(_pixelBufferPool);
        _pixelBufferPool = nil;
    }
    if (_texture) {
        CVBufferRelease(_texture);
        _texture = nil;
    }
    if (_textureCache) {
        CFRelease(_textureCache);
        _textureCache = nil;
    }
    [EAGLContext setCurrentContext:preContext];
    _glContext = nil;
}

- (EAGLContext*)setCurrentContext:(EAGLContext*) context {
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

- (BOOL)creatPixelBufferWithSize:(uint32_t) width height:(uint32_t) height {
    if(!_glContext) {
        return NO;
    }
    _width = width;
    _height = height;
    if (![self createTextureCache]) {
        return NO;
    }
    if (![self createPixelBuffer]) {
        return NO;
    }
    return [self createPixelBuffer];
}

- (BOOL)createPixelBuffer {
    if (!_pixelBufferPool || !_textureCache) {
        return NO;
    }
    if (_texture) {
        CVBufferRelease(_texture);
        _texture = nil;
    }
    
    if (_pixelBuffer) {
        CVPixelBufferRelease(_pixelBuffer);
        _pixelBuffer = nil;
    }
    
    if (_frameBuffer > 0) {
        glDeleteFramebuffers(1, &_frameBuffer);
        _frameBuffer = 0;
    }
    EAGLContext* preContext = [self activeContext];
    glDisable(GL_DEPTH_TEST);
    glGenFramebuffers(1, &_frameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer);
    
    BOOL res = NO;
    GLenum status = GL_FRAMEBUFFER_COMPLETE;
    CVReturn ret = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, _pixelBufferPool, &_pixelBuffer);
    if (ret != kCVReturnSuccess) {
        goto end;
    }
    
    ret = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                       _textureCache,
                                                       _pixelBuffer,
                                                       NULL,
                                                       GL_TEXTURE_2D,
                                                       GL_RGBA,
                                                       _width,
                                                       _height,
                                                       GL_BGRA,
                                                       GL_UNSIGNED_BYTE,
                                                       0,
                                                       &_texture);
    if (ret != kCVReturnSuccess) {
        goto end;
    }
    
    glBindTexture(CVOpenGLESTextureGetTarget(_texture), CVOpenGLESTextureGetName(_texture));
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, CVOpenGLESTextureGetName(_texture), 0);
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        goto end;
    }
    res = YES;
end:
    [self setCurrentContext:preContext];
    return res;
}

- (BOOL)createPixelBufferPool {
    if (_pixelBufferPool) {
        return YES;
    }
    
    CVReturn result = kCVReturnError;
    CFDictionaryRef empty = NULL;
    CFMutableDictionaryRef attrs = NULL;
    CFNumberRef width = NULL;
    CFNumberRef height = NULL;
    CFNumberRef pixelFormat = NULL;
    do {
        attrs = CFDictionaryCreateMutable(kCFAllocatorDefault, 4, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        
        if (!attrs) {
            break;
        }
        
        width = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &_width);
        height = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &_height);
        if (!width || !height) {
            break;
        }
        CFDictionarySetValue(attrs, kCVPixelBufferWidthKey, width);
        CFDictionarySetValue(attrs, kCVPixelBufferHeightKey, height);
        
        int32_t pixelFormatValue = kCVPixelFormatType_32BGRA;
        pixelFormat = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pixelFormatValue);
        if (!pixelFormat) {
            break;
        }
        CFDictionarySetValue(attrs, kCVPixelBufferPixelFormatTypeKey, pixelFormat);
        
        empty = CFDictionaryCreate(kCFAllocatorDefault, NULL, NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        if (!empty) {
            break;
        }
        CFDictionarySetValue(attrs, kCVPixelBufferIOSurfacePropertiesKey, empty);
        
        result = CVPixelBufferPoolCreate(kCFAllocatorDefault, NULL, attrs, &_pixelBufferPool);
    } while (0);

    ReleaseRef(empty);
    ReleaseRef(attrs);
    ReleaseRef(width);
    ReleaseRef(height);
    ReleaseRef(pixelFormat);
    return result == kCVReturnSuccess;
}

- (BOOL)createTextureCache {
    if (_textureCache) {
        CFRelease(_textureCache);
        _textureCache = nil;
    }
    return CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, _glContext, NULL, &_textureCache);
}
@end

namespace slark {

iOSEGLContext::iOSEGLContext() {
    @autoreleasepool {
        context_ = (void*)(CFBridgingRetain([EGLContext new]));
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
        inited_ = [(__bridge EGLContext*)context_ initWithContext:(__bridge EAGLContext*) context];
        if (!inited_) {
            release();
        }
        return inited_;
    }
}

bool iOSEGLContext::createBuffer(uint32_t width, uint32_t height) {
    @autoreleasepool {
        return [(__bridge EGLContext*)context_ creatPixelBufferWithSize:width height:height];
    }
}

void iOSEGLContext::acttachContext() {
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

std::unique_ptr<IEGLContext> createEGLContext() {
    return std::make_unique<iOSEGLContext>();
}

}
