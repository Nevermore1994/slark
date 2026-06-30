
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#if !TARGET_IPHONE_SIMULATOR

#import "RenderGLView.h"

#include "Clock.h"
#include "GLProgram.h"
#include "GLShader.h"
#include "VideoInfo.h"
#include "iOSEGLContext.h"

#import <AVFoundation/AVUtilities.h>
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>
#import <atomic>
#import <functional>
#import <memory>

namespace slark {
struct IVideoRender;
struct VideoInfo;
struct AVFrame;
using AVFrameRefPtr = std::shared_ptr<AVFrame>;
using IEGLContextRefPtr = std::shared_ptr<class IEGLContext>;
class GLProgram;
} // namespace slark

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

    void renderEnd() noexcept override {
        pause();
    }

    void pushFrame(AVFrameRefPtr frame) noexcept override {
        if (!frame || !pushFrameFunc) {
            return;
        }
        auto* opaque = frame->opaque;
        frame->opaque = nullptr;
        auto* pixelBuffer = static_cast<CVPixelBufferRef>(opaque);
        if (pixelBuffer) {
            pushFrameFunc(pixelBuffer);
            CVPixelBufferRelease(pixelBuffer);
        }
    }

    CVPixelBufferRef requestRender() noexcept {
        if (auto fn = requestRenderFunc_.load()) {
            try {
                auto frame = std::invoke(*fn);
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

    std::function<void(CVPixelBufferRef)> pushFrameFunc;
    std::function<void(std::shared_ptr<VideoInfo>)> notifyVideoInfoFunc;
    std::function<void(void)> startFunc;
    std::function<void(void)> pauseFunc;
    std::function<void(void)> notifyRenderInfoFunc;
    Clock videoClock_;
};

// Uniform and attribute indices
enum {
    UNIFORM_Y,
    UNIFORM_UV,
    UNIFORM_ROTATION_ANGLE,
    UNIFORM_COLOR_CONVERSION_MATRIX,
    UNIFORM_Y_OFFSET,
    NUM_UNIFORMS
};

enum { ATTRIB_VERTEX, ATTRIB_TEXCOORD, NUM_ATTRIBUTES };

// Color conversion matrices
static const GLfloat kColorConversion601VideoRange[] = {
    1.164f,
    1.164f,
    1.164f,
    0.0f,
    -0.392f,
    2.017f,
    1.596f,
    -0.813f,
    0.0f,
};
static const GLfloat kColorConversion601FullRange[] = {
    1.0f,
    1.0f,
    1.0f,
    0.0f,
    -0.344136f,
    1.772f,
    1.402f,
    -0.714136f,
    0.0f,
};
static const GLfloat kColorConversion709VideoRange[] = {
    1.164f,
    1.164f,
    1.164f,
    0.0f,
    -0.213f,
    2.112f,
    1.793f,
    -0.533f,
    0.0f,
};
static const GLfloat kColorConversion709FullRange[] = {
    1.0f,
    1.0f,
    1.0f,
    0.0f,
    -0.187324f,
    1.8556f,
    1.5748f,
    -0.468124f,
    0.0f,
};

@interface RenderGLView () {
    IEGLContextRefPtr _context;
    std::unique_ptr<GLProgram> _program;
    std::shared_ptr<VideoRender> _videoRenderImpl;
    const GLfloat* _preferredConversion;
    GLfloat _yOffset;
    CVOpenGLESTextureCacheRef _videoTextureCache;
    CAEAGLLayer* _eaglLayer; // 主线程 cache，避免 renderQueue→main 的 sync 调用

    // 实例级 uniform/attribute 位置，避免多实例并存时全局数组互相覆盖
    GLint _uniforms[NUM_UNIFORMS];
    GLuint _attributes[NUM_ATTRIBUTES];

    // Simple state management
    std::atomic<bool> _setupComplete;
    std::atomic<bool> _isDestructing;

    // Texture references for cleanup
    CVOpenGLESTextureRef _lumaTexture;
    CVOpenGLESTextureRef _chromaTexture;
    CVOpenGLESTextureRef _renderTexture;
}

@property (atomic, assign) BOOL isActive;
@property (nonatomic, assign) NSInteger renderInterval;
@property (nonatomic, strong) CADisplayLink* displayLink;
@property (nonatomic, strong) dispatch_queue_t renderQueue;
@property (nonatomic, assign) GLint width;
@property (nonatomic, assign) GLint height;
@property (nonatomic, assign) GLuint frameBuffer;
@property (nonatomic, assign) GLuint renderBuffer;
@property (nonatomic, assign) CVPixelBufferRef preRenderBuffer;
// For screen rotation
@property (nonatomic, assign) GLuint vertexCoordinateBuffer;
@property (nonatomic, assign) GLuint textureCoordinateBuffer;
@end

@implementation RenderGLView

+ (Class)layerClass {
    return [CAEAGLLayer class];
}

- (BOOL)supportsHDR {
    return NO;
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
        // 用 async + 强引用自持有，避免主线程 sync(renderQueue) ↔ renderQueue sync(main) 死锁
        // GL 资源和 preRenderBuffer 必须在 renderQueue 上统一清理
        dispatch_queue_t q = _renderQueue;
        __strong RenderGLView* strongSelf = self;
        dispatch_async(q, ^{
            [strongSelf cleanupOpenGL];
            if (strongSelf->_vertexCoordinateBuffer) {
                glDeleteBuffers(1, &strongSelf->_vertexCoordinateBuffer);
                strongSelf->_vertexCoordinateBuffer = 0;
            }
            if (strongSelf->_textureCoordinateBuffer) {
                glDeleteBuffers(1, &strongSelf->_textureCoordinateBuffer);
                strongSelf->_textureCoordinateBuffer = 0;
            }
            if (strongSelf->_preRenderBuffer) {
                CVPixelBufferRelease(strongSelf->_preRenderBuffer);
                strongSelf->_preRenderBuffer = NULL;
            }
        });
    }
}

#pragma mark - Setup

- (void)setupDefaults {
    _rotation = 0.0;
    _renderInterval = 30;
    _isActive = YES;
    _preferredConversion = kColorConversion709VideoRange;
    _yOffset = 16.0f / 255.0f;
    _setupComplete.store(false);
    _isDestructing.store(false);

    CGFloat scale = [[UIScreen mainScreen] scale];
    _width = static_cast<GLint>(self.bounds.size.width * scale);
    _height = static_cast<GLint>(self.bounds.size.height * scale);

    _preRenderBuffer = NULL;
    _lumaTexture = NULL;
    _chromaTexture = NULL;
    _renderTexture = NULL;
    _vertexCoordinateBuffer = 0;
    _textureCoordinateBuffer = 0;
    _eaglLayer = nil;
}

- (void)setupRenderQueue {
    _renderQueue = dispatch_queue_create("com.slark.render", DISPATCH_QUEUE_SERIAL);
    dispatch_set_target_queue(_renderQueue,
                              dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
}

- (void)setupRenderDelegate {
    _videoRenderImpl = std::make_shared<VideoRender>();
    __weak __typeof(self) weakSelf = self;

    _videoRenderImpl->notifyVideoInfoFunc = [weakSelf](std::shared_ptr<VideoInfo> info) {
        dispatch_async(dispatch_get_main_queue(), ^{
            __strong __typeof(weakSelf) strongSelf = weakSelf;
            if (strongSelf && info && info->fps > 0) {
                strongSelf.renderInterval = info->fps;
            }
        });
    };

    _videoRenderImpl->startFunc = [weakSelf]() {
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (strongSelf) {
            strongSelf.isActive = YES;
        }
    };

    _videoRenderImpl->pauseFunc = [weakSelf]() {
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (strongSelf) {
            strongSelf.isActive = NO;
        }
    };

    _videoRenderImpl->pushFrameFunc = [weakSelf](CVPixelBufferRef pixelBuffer) {
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (!strongSelf || !pixelBuffer) {
            return;
        }
        CVPixelBufferRetain(pixelBuffer);
        dispatch_async(strongSelf->_renderQueue, ^{
            __strong __typeof(weakSelf) s = weakSelf;
            if (s) {
                [s displayPixelBuffer:pixelBuffer];
                [s saveAsPreRenderBuffer:pixelBuffer];
            }
            CVPixelBufferRelease(pixelBuffer);
        });
    };
}

- (void)setupLayer {
    CAEAGLLayer* layer = static_cast<CAEAGLLayer*>(self.layer);
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
    if (_displayLink) {
        return;
    }

    _displayLink = [CADisplayLink displayLinkWithTarget:self
                                               selector:@selector(displayLinkCallback)];
    if (@available(iOS 15.0, *)) {
        _displayLink.preferredFrameRateRange = CAFrameRateRangeMake(MIN(_renderInterval, 24),
                                                                    MAX(_renderInterval, 60),
                                                                    _renderInterval);
    } else {
        _displayLink.preferredFramesPerSecond = _renderInterval;
    }
    [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)stop {
    if (_displayLink) {
        [_displayLink invalidate];
        _displayLink = nil;
    }
}

- (void*)renderImpl {
    auto ptr = std::make_shared<std::weak_ptr<IVideoRender>>(
        std::weak_ptr<IVideoRender>(_videoRenderImpl));
    return static_cast<void*>(new std::weak_ptr<IVideoRender>(*ptr));
}

- (void)setRotation:(double)rotation {
    static constexpr double kDegreesToRadians = M_PI / 180.0;
    _rotation = rotation * kDegreesToRadians;
}

- (void)setRenderInterval:(NSInteger)renderInterval {
    if (_renderInterval != renderInterval) {
        _renderInterval = renderInterval;
        if (@available(iOS 15.0, *)) {
            _displayLink.preferredFrameRateRange = CAFrameRateRangeMake(MIN(renderInterval, 24),
                                                                        MAX(renderInterval, 60),
                                                                        renderInterval);
        } else {
            _displayLink.preferredFramesPerSecond = renderInterval;
        }
    }
}

- (void)layoutSubviews {
    [super layoutSubviews];

    // 主线程写 _eaglLayer，renderQueue 读，加锁保护竞态
    @synchronized(self) {
        _eaglLayer = static_cast<CAEAGLLayer*>(self.layer);
    }

    CGFloat scale = [[UIScreen mainScreen] scale];
    GLint newWidth = static_cast<GLint>(self.bounds.size.width * scale);
    GLint newHeight = static_cast<GLint>(self.bounds.size.height * scale);

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
    if (_isDestructing.load()) {
        return;
    }

    dispatch_async(_renderQueue, ^{
        [self renderFrame];
    });
}

- (void)renderFrame {
    if (!_setupComplete.load() || _isDestructing.load()) {
        return;
    }

    CVPixelBufferRef buffer = _videoRenderImpl->requestRender();
    if (buffer) {
        [self displayPixelBuffer:buffer];
        [self saveAsPreRenderBuffer:buffer];
        CVPixelBufferRelease(buffer);
    }
}

- (void)renderPreBufferIfNeeded {
    if (!_setupComplete.load() || _isDestructing.load()) {
        return;
    }

    if (_preRenderBuffer) {
        NSLog(@"Re-rendering last frame after screen rotation");
        [self displayPixelBuffer:_preRenderBuffer];
    }
}

- (void)saveAsPreRenderBuffer:(CVPixelBufferRef)pixelBuffer {
    if (!pixelBuffer) {
        return;
    }

    if (_preRenderBuffer) {
        CVPixelBufferRelease(_preRenderBuffer);
    }
    _preRenderBuffer = CVPixelBufferRetain(pixelBuffer);
}

#pragma mark - OpenGL Management

- (void)setupOpenGL {
    if (!_context || _isDestructing.load()) {
        return;
    }

    _context->attachContext();

    _program = std::make_unique<GLProgram>(GLShader::vertexShader, GLShader::fragmentShader);
    if (!_program->isValid()) {
        _context->detachContext();
        return;
    }

    auto program = _program->program();
    _attributes[ATTRIB_VERTEX] = static_cast<GLuint>(glGetAttribLocation(program, "position"));
    _attributes[ATTRIB_TEXCOORD] = static_cast<GLuint>(glGetAttribLocation(program, "texCoord"));
    _uniforms[UNIFORM_Y] = glGetUniformLocation(program, "SamplerY");
    _uniforms[UNIFORM_UV] = glGetUniformLocation(program, "SamplerUV");
    _uniforms[UNIFORM_ROTATION_ANGLE] = glGetUniformLocation(program, "preferredRotation");
    _uniforms[UNIFORM_COLOR_CONVERSION_MATRIX] =
        glGetUniformLocation(program, "colorConversionMatrix");

    glGenBuffers(1, &_vertexCoordinateBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, _vertexCoordinateBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 8, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(_attributes[ATTRIB_VERTEX],
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          0,
                          static_cast<void*>(nullptr));
    glEnableVertexAttribArray(_attributes[ATTRIB_VERTEX]);

    static const GLfloat texCoords[] = {0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    glGenBuffers(1, &_textureCoordinateBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, _textureCoordinateBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(texCoords), texCoords, GL_STATIC_DRAW);
    glVertexAttribPointer(_attributes[ATTRIB_TEXCOORD],
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          0,
                          static_cast<void*>(nullptr));
    glEnableVertexAttribArray(_attributes[ATTRIB_TEXCOORD]);

    glBindBuffer(GL_ARRAY_BUFFER, 0);

    auto nativeContext = (__bridge EAGLContext*)_context->nativeContext();
    if (!nativeContext) {
        _context->detachContext();
        return;
    }
    glDisable(GL_DEPTH_TEST);

    [self createFramebuffer];

    _uniforms[UNIFORM_Y_OFFSET] = glGetUniformLocation(_program->program(), "yOffset");

    _program->attach();

    glUniformMatrix3fv(_uniforms[UNIFORM_COLOR_CONVERSION_MATRIX],
                       1,
                       GL_FALSE,
                       _preferredConversion);
    glUniform1f(_uniforms[UNIFORM_Y_OFFSET], _yOffset);
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

    // 使用主线程 cache 的 layer，renderbufferStorage 要求 context 为 current，
    // 此处已在 renderQueue 上 attachContext，直接调用无需切回主线程
    CAEAGLLayer* layer = nil;
    @synchronized(self) {
        layer = _eaglLayer ?: static_cast<CAEAGLLayer*>(self.layer);
    }
    BOOL success = [nativeContext renderbufferStorage:GL_RENDERBUFFER fromDrawable:layer];

    if (success) {
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                  GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER,
                                  _renderBuffer);

        // 创建 texture cache（仅在未创建时创建，避免重复创建泄漏）
        if (!_videoTextureCache) {
            CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault,
                                                        NULL,
                                                        nativeContext,
                                                        NULL,
                                                        &_videoTextureCache);
            if (err != noErr) {
                LogE("Error at CVOpenGLESTextureCacheCreate {}", err);
            }
        }
    }
}

- (void)rebuildFramebuffer {
    if (!_setupComplete.load()) {
        return;
    }

    _context->attachContext();

    if (_frameBuffer) {
        glDeleteFramebuffers(1, &_frameBuffer);
        _frameBuffer = 0;
    }
    if (_renderBuffer) {
        glDeleteRenderbuffers(1, &_renderBuffer);
        _renderBuffer = 0;
    }

    [self createFramebuffer];

    _context->detachContext();
}

- (void)cleanupOpenGL {
    if (!_context) {
        return;
    }

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
    if (!pixelBuffer || !_setupComplete.load()) {
        return;
    }

    _context->attachContext();
    _program->attach();

    auto formatType = CVPixelBufferGetPixelFormatType(pixelBuffer);
    auto frameWidth = static_cast<GLsizei>(CVPixelBufferGetWidth(pixelBuffer));
    auto frameHeight = static_cast<GLsizei>(CVPixelBufferGetHeight(pixelBuffer));

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
    _program->detach(); // 先 detach program，再 detach context
    _context->detachContext();
}

- (BOOL)uploadYUVTexture:(CVPixelBufferRef)pixelBuffer {
    auto frameWidth = static_cast<GLsizei>(CVPixelBufferGetWidth(pixelBuffer));
    auto frameHeight = static_cast<GLsizei>(CVPixelBufferGetHeight(pixelBuffer));

    // 根据 format type 和 YCbCrMatrix 选择正确的颜色矩阵与 yOffset
    bool isFullRange = (CVPixelBufferGetPixelFormatType(pixelBuffer) ==
                        kCVPixelFormatType_420YpCbCr8BiPlanarFullRange);
    _yOffset = isFullRange ? 0.0f : (16.0f / 255.0f);

    CFTypeRef colorAttachments =
        CVBufferCopyAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, NULL);
    bool is601 = colorAttachments && CFStringCompare(static_cast<CFStringRef>(colorAttachments),
                                                     kCVImageBufferYCbCrMatrix_ITU_R_601_4,
                                                     0) == kCFCompareEqualTo;
    if (colorAttachments) {
        CFRelease(colorAttachments);
    }
    if (is601) {
        _preferredConversion =
            isFullRange ? kColorConversion601FullRange : kColorConversion601VideoRange;
    } else {
        _preferredConversion =
            isFullRange ? kColorConversion709FullRange : kColorConversion709VideoRange;
    }

    // Y texture
    glActiveTexture(GL_TEXTURE0);
    CVReturn err = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                                _videoTextureCache,
                                                                pixelBuffer,
                                                                NULL,
                                                                GL_TEXTURE_2D,
                                                                GL_RED_EXT,
                                                                frameWidth,
                                                                frameHeight,
                                                                GL_RED_EXT,
                                                                GL_UNSIGNED_BYTE,
                                                                0,
                                                                &_lumaTexture);

    if (err != kCVReturnSuccess) {
        LogE("upload error");
        return NO;
    }

    glBindTexture(CVOpenGLESTextureGetTarget(_lumaTexture), CVOpenGLESTextureGetName(_lumaTexture));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUniform1i(_uniforms[UNIFORM_Y], 0);

    // UV texture
    glActiveTexture(GL_TEXTURE1);
    err = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                       _videoTextureCache,
                                                       pixelBuffer,
                                                       NULL,
                                                       GL_TEXTURE_2D,
                                                       GL_RG_EXT,
                                                       frameWidth / 2,  // NOLINT
                                                       frameHeight / 2, // NOLINT
                                                       GL_RG_EXT,
                                                       GL_UNSIGNED_BYTE,
                                                       1,
                                                       &_chromaTexture);

    if (err != kCVReturnSuccess) {
        CFRelease(_lumaTexture);
        _lumaTexture = NULL;
        LogE("upload error");
        return NO;
    }

    glBindTexture(CVOpenGLESTextureGetTarget(_chromaTexture),
                  CVOpenGLESTextureGetName(_chromaTexture));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glUniform1i(_uniforms[UNIFORM_UV], 1);
    glUniformMatrix3fv(_uniforms[UNIFORM_COLOR_CONVERSION_MATRIX],
                       1,
                       GL_FALSE,
                       _preferredConversion);
    glUniform1f(_uniforms[UNIFORM_Y_OFFSET], _yOffset);

    return YES;
}

- (BOOL)uploadRGBATexture:(CVPixelBufferRef)pixelBuffer {
    auto frameWidth = static_cast<GLsizei>(CVPixelBufferGetWidth(pixelBuffer));
    auto frameHeight = static_cast<GLsizei>(CVPixelBufferGetHeight(pixelBuffer));

    glActiveTexture(GL_TEXTURE0);
    CVReturn err = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                                _videoTextureCache,
                                                                pixelBuffer,
                                                                NULL,
                                                                GL_TEXTURE_2D,
                                                                GL_RGBA,
                                                                frameWidth,
                                                                frameHeight,
                                                                GL_BGRA,
                                                                GL_UNSIGNED_BYTE,
                                                                0,
                                                                &_renderTexture);

    if (err != kCVReturnSuccess) {
        return NO;
    }

    glBindTexture(CVOpenGLESTextureGetTarget(_renderTexture),
                  CVOpenGLESTextureGetName(_renderTexture));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return YES;
}

- (void)renderTextures:(GLint)frameWidth height:(GLint)frameHeight {
    auto nativeContext = (__bridge EAGLContext*)_context->nativeContext();

    glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer);
    glViewport(0, 0, _width, _height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Calculate aspect ratio preserving viewport
    CGRect viewBounds = CGRectMake(0, 0, _width, _height);
    CGSize contentSize = CGSizeMake(frameWidth, frameHeight);
    CGRect vertexSamplingRect = AVMakeRectWithAspectRatioInsideRect(contentSize, viewBounds);

    CGSize normalizedSamplingSize;
    CGSize cropScaleAmount = CGSizeMake(vertexSamplingRect.size.width / viewBounds.size.width,
                                        vertexSamplingRect.size.height / viewBounds.size.height);

    if (cropScaleAmount.width > cropScaleAmount.height) {
        normalizedSamplingSize.width = 1.0;
        normalizedSamplingSize.height = cropScaleAmount.height / cropScaleAmount.width;
    } else {
        normalizedSamplingSize.width = cropScaleAmount.width / cropScaleAmount.height;
        normalizedSamplingSize.height = 1.0;
    }

    // Vertex data for quad with aspect ratio
    GLfloat vertices[] = {static_cast<GLfloat>(-1 * normalizedSamplingSize.width),
                          static_cast<GLfloat>(-1 * normalizedSamplingSize.height),
                          static_cast<GLfloat>(normalizedSamplingSize.width),
                          static_cast<GLfloat>(-1 * normalizedSamplingSize.height),
                          static_cast<GLfloat>(-1 * normalizedSamplingSize.width),
                          static_cast<GLfloat>(normalizedSamplingSize.height),
                          static_cast<GLfloat>(normalizedSamplingSize.width),
                          static_cast<GLfloat>(normalizedSamplingSize.height)};

    glBindBuffer(GL_ARRAY_BUFFER, _vertexCoordinateBuffer);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(float) * 8, vertices);
    glUniform1f(_uniforms[UNIFORM_ROTATION_ANGLE], static_cast<GLfloat>(_rotation));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    [nativeContext presentRenderbuffer:GL_RENDERBUFFER];
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

- (void)cleanupTextures {
    if (_lumaTexture) {
        CFRelease(_lumaTexture);
        _lumaTexture = NULL;
    }
    if (_chromaTexture) {
        CFRelease(_chromaTexture);
        _chromaTexture = NULL;
    }
    if (_renderTexture) {
        CFRelease(_renderTexture);
        _renderTexture = NULL;
    }
    if (_videoTextureCache) {
        CVOpenGLESTextureCacheFlush(_videoTextureCache, 0);
    }
}

@end

#endif // !TARGET_IPHONE_SIMULATOR
