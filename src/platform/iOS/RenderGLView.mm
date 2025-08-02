//
//  RenderGLView.mm
//  slark
//
//  Created by Nevermore on 2024/10/30.
//

#import <AVFoundation/AVUtilities.h>
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>
#import <functional>
#import <memory>
#import <atomic>
#import "RenderGLView.h"
#include "video/VideoInfo.h"
#include "opengles/GLShader.h"
#include "opengles/GLProgram.h"

namespace slark {
    class IVideoRender;
    struct VideoInfo;
    struct AVFrame;
    using AVFrameRefPtr = std::shared_ptr<AVFrame>;
    using IEGLContextRefPtr = std::shared_ptr<class IEGLContext>;
    class GLProgram;
}

using namespace slark;

// Simplified VideoRender implementation
struct VideoRender : public IVideoRender {
    
    void notifyVideoInfo(std::shared_ptr<VideoInfo> videoInfo) noexcept override {
        if (notifyVideoInfoFunc) {
            notifyVideoInfoFunc(videoInfo);
        }
    }
    
    void notifyRenderInfo() noexcept override {
        if (notifyRenderInfoFunc) {
            notifyRenderInfoFunc();
        }
    }
    
    void start() noexcept override {
        videoClock_.start();
        if (startFunc) {
            startFunc();
        }
    }
    
    void pause() noexcept override {
        videoClock_.pause();
        if (pauseFunc) {
            pauseFunc();
        }
    }
    
    void pushVideoFrameRender(AVFrameRefPtr frame) noexcept override {
        if (pushVideoFrameRenderFunc && frame) {
            auto* opaque = frame->opaque;
            frame->opaque = nullptr;
            pushVideoFrameRenderFunc(opaque);
        }
    }
    
    void renderEnd() noexcept override {
        pause();
    }
    
    CVPixelBufferRef requestRender() noexcept {
        if (auto requestRenderFunc = requestRenderFunc_.load()) {
            try {
                auto frame = std::invoke(*requestRenderFunc);
                if (frame) {
                    auto* opaque = frame->opaque;
                    frame->opaque = nullptr;
                    return static_cast<CVPixelBufferRef>(opaque);
                }
            } catch (...) {
                LogE("Error in requestRender");
            }
        }
        return nil;
    }
    
    std::function<void(std::shared_ptr<VideoInfo>)> notifyVideoInfoFunc;
    std::function<void(void)> startFunc;
    std::function<void(void)> pauseFunc;
    std::function<void(void)> notifyRenderInfoFunc;
    std::function<void(void*)> pushVideoFrameRenderFunc;
};

// Uniform and attribute indices
enum { UNIFORM_Y, UNIFORM_UV, UNIFORM_ROTATION_ANGLE, UNIFORM_COLOR_CONVERSION_MATRIX, NUM_UNIFORMS };
GLint uniforms[NUM_UNIFORMS];

enum { ATTRIB_VERTEX, ATTRIB_TEXCOORD, NUM_ATTRIBUTES };
GLuint attributes[NUM_ATTRIBUTES];

// Color conversion matrices
static const GLfloat kColorConversion601VideoRange[] = {
    1.164f,  1.164f, 1.164f, 0.0f, -0.392f, 2.017f, 1.596f, -0.813f, 0.0f,
};
static const GLfloat kColorConversion709VideoRange[] = {
    1.164f,  1.164f, 1.164f, 0.0f, -0.213f, 2.112f, 1.793f, -0.533f, 0.0f,
};

@interface RenderGLView() {
    IEGLContextRefPtr _context;
    std::unique_ptr<GLProgram> _program;
    std::shared_ptr<VideoRender> _videoRenderImpl;
    const GLfloat* _preferredConversion;
    CVOpenGLESTextureCacheRef _videoTextureCache;
    
    // Simple state management
    std::atomic<bool> _setupComplete;
    std::atomic<bool> _isDestructing;
    
    // Texture references for cleanup
    CVOpenGLESTextureRef _lumaTexture;
    CVOpenGLESTextureRef _chromaTexture;
    CVOpenGLESTextureRef _renderTexture;
}

@property(nonatomic, strong) CADisplayLink* displayLink;
@property(nonatomic, strong) dispatch_queue_t renderQueue;
@property(nonatomic, assign) GLint width;
@property(nonatomic, assign) GLint height;
@property(nonatomic, assign) GLuint frameBuffer;
@property(nonatomic, assign) GLuint renderBuffer;
@property(nonatomic, assign) CVPixelBufferRef pendingBuffer;
@property(nonatomic, assign) CVPixelBufferRef preRenderBuffer; // For screen rotation

@end

@implementation RenderGLView

+ (Class)layerClass {
    return [CAEAGLLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    if (self = [super initWithFrame:frame]) {
        [self setupDefaults];
        [self setupRenderQueue];
        [self setupRenderDelegate];
        [self setupLayer];
    }
    return self;
}

- (void)dealloc {
    _isDestructing.store(true);
    [self stop];
    
    if (_renderQueue) {
        dispatch_sync(_renderQueue, ^{
            [self cleanupOpenGL];
        });
    }
    
    // Clean up pixel buffers
    if (_pendingBuffer) {
        CVPixelBufferRelease(_pendingBuffer);
        _pendingBuffer = NULL;
    }
    if (_preRenderBuffer) {
        CVPixelBufferRelease(_preRenderBuffer);
        _preRenderBuffer = NULL;
    }
}

#pragma mark - Setup

- (void)setupDefaults {
    _renderInterval = 30;
    _isActive = YES;
    _preferredConversion = kColorConversion709VideoRange;
    _setupComplete.store(false);
    _isDestructing.store(false);
    
    CGFloat scale = [[UIScreen mainScreen] scale];
    _width = self.bounds.size.width * scale;
    _height = self.bounds.size.height * scale;
    
    _pendingBuffer = NULL;
    _preRenderBuffer = NULL;
    _lumaTexture = NULL;
    _chromaTexture = NULL;
    _renderTexture = NULL;
}

- (void)setupRenderQueue {
    _renderQueue = dispatch_queue_create("com.slark.render", DISPATCH_QUEUE_SERIAL);
    dispatch_set_target_queue(_renderQueue, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
}

- (void)setupRenderDelegate {
    _videoRenderImpl = std::make_shared<VideoRender>();
    __weak __typeof(self) weakSelf = self;
    
    _videoRenderImpl->notifyVideoInfoFunc = [weakSelf](std::shared_ptr<VideoInfo> info) {
        dispatch_sync(dispatch_get_main_queue(),^{
            __strong __typeof(weakSelf) strongSelf = weakSelf;
            if (strongSelf && info && info->fps > 0) {
                strongSelf.renderInterval = info->fps;
            }
        });
    };
    
    _videoRenderImpl->startFunc = [weakSelf]() {
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (strongSelf) strongSelf.isActive = YES;
    };
    
    _videoRenderImpl->pauseFunc = [weakSelf]() {
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (strongSelf) strongSelf.isActive = NO;
    };
    
    _videoRenderImpl->pushVideoFrameRenderFunc = [weakSelf](void* buffer) {
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (strongSelf) {
            [strongSelf pushRenderPixelBuffer:(CVPixelBufferRef)buffer];
        }
    };
}

- (void)setupLayer {
    CAEAGLLayer* layer = (CAEAGLLayer*)self.layer;
    layer.opaque = YES;
    layer.contentsScale = [[UIScreen mainScreen] scale];
    layer.drawableProperties = @{
        kEAGLDrawablePropertyRetainedBacking: @NO,
        kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8
    };
}

#pragma mark - Public Interface

- (void)setContext:(slark::IEGLContextRefPtr)context {
    _context = context;
    dispatch_async(_renderQueue, ^{
        [self setupOpenGL];
    });
}

- (void)start {
    if (_displayLink) return;
    
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(displayLinkCallback)];
    _displayLink.preferredFramesPerSecond = _renderInterval;
    [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)stop {
    if (_displayLink) {
        [_displayLink invalidate];
        _displayLink = nil;
    }
}

- (std::weak_ptr<IVideoRender>)renderImpl {
    return std::weak_ptr<IVideoRender>(_videoRenderImpl);
}

- (void)setRenderInterval:(NSInteger)renderInterval {
    if (_renderInterval != renderInterval) {
        _renderInterval = renderInterval;
        _displayLink.preferredFramesPerSecond = renderInterval;
    }
}

- (void)layoutSubviews {
    [super layoutSubviews];
    
    CGFloat scale = [[UIScreen mainScreen] scale];
    GLint newWidth = self.bounds.size.width * scale;
    GLint newHeight = self.bounds.size.height * scale;
    
    if (newWidth != _width || newHeight != _height) {
        _width = newWidth;
        _height = newHeight;
        NSLog(@"Screen rotation detected, new size: %d x %d", _width, _height);
        
        dispatch_async(_renderQueue, ^{
            [self rebuildFramebuffer];
            // Render the last frame after screen rotation
            [self renderPreBufferIfNeeded];
        });
    }
}

#pragma mark - Rendering

- (void)displayLinkCallback {
    if (!_isActive || _isDestructing.load()) return;
    
    dispatch_async(_renderQueue, ^{
        [self renderFrame];
    });
}

- (void)pushRenderPixelBuffer:(CVPixelBufferRef)pixelBuffer {
    if (!pixelBuffer || _isDestructing.load()) return;
    
    // Replace pending buffer
    if (_pendingBuffer) {
        CVPixelBufferRelease(_pendingBuffer);
    }
    _pendingBuffer = CVPixelBufferRetain(pixelBuffer);
    CVPixelBufferRelease(pixelBuffer);
    
    dispatch_async(_renderQueue, ^{
        [self renderPendingBuffer];
    });
}

- (void)renderFrame {
    if (!_setupComplete.load() || _isDestructing.load()) return;
    
    CVPixelBufferRef buffer = _videoRenderImpl->requestRender();
    if (buffer) {
        [self displayPixelBuffer:buffer];
        [self saveAsPreRenderBuffer:buffer];
        CVPixelBufferRelease(buffer);
    }
}

- (void)renderPendingBuffer {
    if (!_setupComplete.load() || _isDestructing.load()) return;
    
    if (_pendingBuffer) {
        CVPixelBufferRef buffer = _pendingBuffer;
        _pendingBuffer = NULL;
        [self displayPixelBuffer:buffer];
        [self saveAsPreRenderBuffer:buffer];
        CVPixelBufferRelease(buffer);
    }
}

- (void)renderPreBufferIfNeeded {
    if (!_setupComplete.load() || _isDestructing.load()) return;
    
    if (_preRenderBuffer) {
        NSLog(@"Re-rendering last frame after screen rotation");
        [self displayPixelBuffer:_preRenderBuffer];
    }
}

- (void)saveAsPreRenderBuffer:(CVPixelBufferRef)pixelBuffer {
    if (!pixelBuffer) return;
    
    if (_preRenderBuffer) {
        CVPixelBufferRelease(_preRenderBuffer);
    }
    _preRenderBuffer = CVPixelBufferRetain(pixelBuffer);
}

#pragma mark - OpenGL Management

- (void)setupOpenGL {
    if (!_context || _isDestructing.load()) return;
    
    _context->attachContext();
    
    // Setup program
    _program = std::make_unique<GLProgram>(GLShader::vertexShader, GLShader::fragmentShader);
    if (!_program->isValid()) {
        _context->detachContext();
        return;
    }
    
    // Get locations
    auto program = _program->program();
    attributes[ATTRIB_VERTEX] = glGetAttribLocation(program, "position");
    attributes[ATTRIB_TEXCOORD] = glGetAttribLocation(program, "texCoord");
    uniforms[UNIFORM_Y] = glGetUniformLocation(program, "SamplerY");
    uniforms[UNIFORM_UV] = glGetUniformLocation(program, "SamplerUV");
    uniforms[UNIFORM_ROTATION_ANGLE] = glGetUniformLocation(program, "preferredRotation");
    uniforms[UNIFORM_COLOR_CONVERSION_MATRIX] = glGetUniformLocation(program, "colorConversionMatrix");
    
    if (_context == nullptr) {
        return;
    }
    auto nativeContext = (__bridge EAGLContext*)_context->nativeContext();
    if (!nativeContext) {
        return;
    }
    glDisable(GL_DEPTH_TEST);
    
    [self createFramebuffer];
    if (!_videoTextureCache) {
        CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, nativeContext, NULL, &_videoTextureCache);
        if (err != noErr) {
            LogE("Error at CVOpenGLESTextureCacheCreate {}", err);
        }
    }
    
    _program->attach();
    
    glUniform1i(uniforms[UNIFORM_Y], 0);
    glUniform1i(uniforms[UNIFORM_UV], 1);
    glUniform1f(uniforms[UNIFORM_ROTATION_ANGLE], 0);

    glUniformMatrix3fv(uniforms[UNIFORM_COLOR_CONVERSION_MATRIX], 1, GL_FALSE, _preferredConversion);
    _program->detach();
    _context->detachContext();
    _setupComplete.store(true);
}

- (void)createFramebuffer {
    auto nativeContext = (__bridge EAGLContext*)_context->nativeContext();
    
    glGenFramebuffers(1, &_frameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer);
    
    glGenRenderbuffers(1, &_renderBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _renderBuffer);
    
    // Access layer on main thread
    __block BOOL success = NO;
    dispatch_sync(dispatch_get_main_queue(), ^{
        CAEAGLLayer* layer = (CAEAGLLayer*)self.layer;
        success = [nativeContext renderbufferStorage:GL_RENDERBUFFER fromDrawable:layer];
    });
    
    if (success) {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _renderBuffer);
        
        // Create texture cache
        CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, nativeContext, NULL, &_videoTextureCache);
    }
}

- (void)rebuildFramebuffer {
    if (!_setupComplete.load()) return;
    
    _context->attachContext();
    
    // Delete old
    if (_frameBuffer) {
        glDeleteFramebuffers(1, &_frameBuffer);
        _frameBuffer = 0;
    }
    if (_renderBuffer) {
        glDeleteRenderbuffers(1, &_renderBuffer);
        _renderBuffer = 0;
    }
    
    // Recreate
    [self createFramebuffer];
    
    _context->detachContext();
}

- (void)cleanupOpenGL {
    if (!_context) return;
    
    _context->attachContext();
    
    [self cleanupTextures];
    
    if (_frameBuffer) {
        glDeleteFramebuffers(1, &_frameBuffer);
        _frameBuffer = 0;
    }
    if (_renderBuffer) {
        glDeleteRenderbuffers(1, &_renderBuffer);
        _renderBuffer = 0;
    }
    if (_videoTextureCache) {
        CFRelease(_videoTextureCache);
        _videoTextureCache = NULL;
    }
    
    _context->detachContext();
}

#pragma mark - Pixel Buffer Display

- (void)displayPixelBuffer:(CVPixelBufferRef)pixelBuffer {
    if (!pixelBuffer || !_setupComplete.load()) return;
    
    _context->attachContext();
    
    auto formatType = CVPixelBufferGetPixelFormatType(pixelBuffer);
    auto frameWidth = static_cast<GLint>(CVPixelBufferGetWidth(pixelBuffer));
    auto frameHeight = static_cast<GLint>(CVPixelBufferGetHeight(pixelBuffer));
    
    BOOL success = NO;
    if (formatType == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange ||
        formatType == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
        success = [self uploadYUVTexture:pixelBuffer];
    } else if (formatType == kCVPixelFormatType_32BGRA) {
        success = [self uploadRGBATexture:pixelBuffer];
    }
    
    if (success) {
        [self renderTextures:frameWidth height:frameHeight];
    }
    
    [self cleanupTextures];
    _context->detachContext();
}

- (BOOL)uploadYUVTexture:(CVPixelBufferRef)pixelBuffer {
    auto frameWidth = CVPixelBufferGetWidth(pixelBuffer);
    auto frameHeight = CVPixelBufferGetHeight(pixelBuffer);
    
    // Determine color conversion matrix
    CFTypeRef colorAttachments = CVBufferCopyAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, NULL);
    if (colorAttachments) {
        if (CFStringCompare(static_cast<CFStringRef>(colorAttachments), kCVImageBufferYCbCrMatrix_ITU_R_601_4, 0) == kCFCompareEqualTo) {
            _preferredConversion = kColorConversion601VideoRange;
        } else {
            _preferredConversion = kColorConversion709VideoRange;
        }
        CFRelease(colorAttachments);
    }
    
    // Y texture
    glActiveTexture(GL_TEXTURE0);
    CVReturn err = CVOpenGLESTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, _videoTextureCache, pixelBuffer, NULL,
        GL_TEXTURE_2D, GL_RED_EXT, frameWidth, frameHeight,
        GL_RED_EXT, GL_UNSIGNED_BYTE, 0, &_lumaTexture);
    
    if (err != kCVReturnSuccess) {
        LogE("upload error");
        return NO;
    }
    
    glBindTexture(CVOpenGLESTextureGetTarget(_lumaTexture), CVOpenGLESTextureGetName(_lumaTexture));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // UV texture
    glActiveTexture(GL_TEXTURE1);
    err = CVOpenGLESTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, _videoTextureCache, pixelBuffer, NULL,
        GL_TEXTURE_2D, GL_RG_EXT, frameWidth / 2, frameHeight / 2,
        GL_RG_EXT, GL_UNSIGNED_BYTE, 1, &_chromaTexture);
    
    if (err != kCVReturnSuccess) {
        CFRelease(_lumaTexture);
        _lumaTexture = NULL;
        LogE("upload error");
        return NO;
    }
    
    glBindTexture(CVOpenGLESTextureGetTarget(_chromaTexture), CVOpenGLESTextureGetName(_chromaTexture));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Update color conversion matrix
    _program->attach();
    glUniform1f(uniforms[UNIFORM_ROTATION_ANGLE], 0);
    glUniformMatrix3fv(uniforms[UNIFORM_COLOR_CONVERSION_MATRIX], 1, GL_FALSE, _preferredConversion);
    _program->detach();
    
    return YES;
}

- (BOOL)uploadRGBATexture:(CVPixelBufferRef)pixelBuffer {
    auto frameWidth = CVPixelBufferGetWidth(pixelBuffer);
    auto frameHeight = CVPixelBufferGetHeight(pixelBuffer);

    _program->attach();
    glActiveTexture(GL_TEXTURE0);
    CVReturn err = CVOpenGLESTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, _videoTextureCache, pixelBuffer, NULL,
        GL_TEXTURE_2D, GL_RGBA, frameWidth, frameHeight,
        GL_BGRA, GL_UNSIGNED_BYTE, 0, &_renderTexture);
    
    if (err != kCVReturnSuccess) return NO;
    
    glBindTexture(CVOpenGLESTextureGetTarget(_renderTexture), CVOpenGLESTextureGetName(_renderTexture));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    _program->detach();
    return YES;
}

- (void)renderTextures:(GLint)frameWidth height:(GLint)frameHeight {
    auto nativeContext = (__bridge EAGLContext*)_context->nativeContext();

    glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer);
    glViewport(0, 0, _width, _height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    _program->attach();
    
    // Calculate aspect ratio preserving viewport
    CGRect viewBounds = CGRectMake(0, 0, _width, _height);
    CGSize contentSize = CGSizeMake(frameWidth, frameHeight);
    CGRect vertexSamplingRect = AVMakeRectWithAspectRatioInsideRect(contentSize, viewBounds);
    
    CGSize normalizedSamplingSize;
    CGSize cropScaleAmount = CGSizeMake(
        vertexSamplingRect.size.width / viewBounds.size.width,
        vertexSamplingRect.size.height / viewBounds.size.height
    );
    
    if (cropScaleAmount.width > cropScaleAmount.height) {
        normalizedSamplingSize.width = 1.0;
        normalizedSamplingSize.height = cropScaleAmount.height / cropScaleAmount.width;
    } else {
        normalizedSamplingSize.width = cropScaleAmount.width / cropScaleAmount.height;
        normalizedSamplingSize.height = 1.0;
    }
    
    // Vertex data for quad with aspect ratio
    GLfloat vertices[] = {
        (GLfloat)(-1 * normalizedSamplingSize.width), (GLfloat)(-1 * normalizedSamplingSize.height),
        (GLfloat)(normalizedSamplingSize.width), (GLfloat)(-1 * normalizedSamplingSize.height),
        (GLfloat)(-1 * normalizedSamplingSize.width), (GLfloat)(normalizedSamplingSize.height),
        (GLfloat)(normalizedSamplingSize.width), (GLfloat)(normalizedSamplingSize.height)
    };
    
    NSLog(@"vertices: [%f, %f] [%f, %f] [%f, %f] [%f, %f]",
          vertices[0], vertices[1],
          vertices[2], vertices[3],
          vertices[4], vertices[5],
          vertices[6], vertices[7]);
    
    static const GLfloat texCoords[] = {
        0.0f, 1.0f,  1.0f, 1.0f,  0.0f, 0.0f,  1.0f, 0.0f
    };
    
    glVertexAttribPointer(attributes[ATTRIB_VERTEX], 2, GL_FLOAT, 0, 0, vertices);
    glEnableVertexAttribArray(attributes[ATTRIB_VERTEX]);
    
    glVertexAttribPointer(attributes[ATTRIB_TEXCOORD], 2, GL_FLOAT, 0, 0, texCoords);
    glEnableVertexAttribArray(attributes[ATTRIB_TEXCOORD]);
    
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    [nativeContext presentRenderbuffer:GL_RENDERBUFFER];
    _program->detach();
}

- (void)cleanupTextures {
    if (_lumaTexture) { CFRelease(_lumaTexture); _lumaTexture = NULL; }
    if (_chromaTexture) { CFRelease(_chromaTexture); _chromaTexture = NULL; }
    if (_renderTexture) { CFRelease(_renderTexture); _renderTexture = NULL; }
    if (_videoTextureCache) {
        CVOpenGLESTextureCacheFlush(_videoTextureCache, 0);
    }
}

@end
