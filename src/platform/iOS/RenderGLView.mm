//
//  RenderGLView.mm
//  slark
//
//  Optimized version with thread safety improvements
//  Created by Nevermore on 2024/10/30.
//

#import <AVFoundation/AVUtilities.h>
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>
#import <functional>
#import <memory>
#import <atomic>
#import <mutex>
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

// Thread-safe VideoRender implementation with improved error handling
struct VideoRender : public IVideoRender {
private:
    std::atomic<bool> active_{false};
    std::mutex callbackMutex_;
    
public:
    void notifyVideoInfo(std::shared_ptr<VideoInfo> videoInfo) noexcept override {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (notifyVideoInfoFunc && active_.load()) {
            notifyVideoInfoFunc(videoInfo);
        }
    }
    
    void notifyRenderInfo() noexcept override {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (notifyRenderInfoFunc && active_.load()) {
            notifyRenderInfoFunc();
        }
    }
    
    void start() noexcept override {
        active_.store(true);
        videoClock_.start();
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (startFunc) {
            startFunc();
        }
    }
    
    void pause() noexcept override {
        videoClock_.pause();
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (pauseFunc) {
            pauseFunc();
        }
    }
    
    void pushVideoFrameRender(AVFrameRefPtr frame) noexcept override {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (pushVideoFrameRenderFunc && active_.load()) {
            pushVideoFrameRenderFunc(frame);
        }
    }
    
    void renderEnd() noexcept override {
        active_.store(false);
        pause();
    }
    
    ~VideoRender() override {
        active_.store(false);
        std::lock_guard<std::mutex> lock(callbackMutex_);
        notifyVideoInfoFunc = nullptr;
        startFunc = nullptr;
        pauseFunc = nullptr;
        notifyRenderInfoFunc = nullptr;
        pushVideoFrameRenderFunc = nullptr;
    }
    
    CVPixelBufferRef requestRender() noexcept {
        if (!active_.load()) {
            return nil;
        }
        
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
    std::function<void(AVFrameRefPtr)> pushVideoFrameRenderFunc;
};

// Uniform index.
enum
{
    UNIFORM_Y,
    UNIFORM_UV,
    UNIFORM_ROTATION_ANGLE,
    UNIFORM_COLOR_CONVERSION_MATRIX,
    NUM_UNIFORMS
};
GLint uniforms[NUM_UNIFORMS];

// Attribute index.
enum
{
    ATTRIB_VERTEX,
    ATTRIB_TEXCOORD,
    NUM_ATTRIBUTES
};
GLuint attributes[NUM_ATTRIBUTES];

// Color conversion matrices (same as original)
static const GLfloat kColorConversion601VideoRange[] = {
    1.164f,  1.164f, 1.164f,
    0.0f, -0.392f, 2.017f,
    1.596f, -0.813f,   0.0f,
};

static const GLfloat kColorConversion601FullRange[] = {
    1.0f,  1.0f, 1.0f,
    0.0f, -0.343f, 1.765f,
    1.4f, -0.711f, 0.0f,
};

static const GLfloat kColorConversion709VideoRange[] = {
    1.164f,  1.164f, 1.164f,
    0.0f, -0.213f, 2.112f,
    1.793f, -0.533f,   0.0f,
};

static const GLfloat kColorConversion709FullRange[] = {
    1.000f,  1.000f, 1.000f,
    0.000f, -0.187f, 2.018f,
    1.574f, -0.468f, 0.000f,
};

@interface RenderGLView()
{
    IEGLContextRefPtr _context;
    std::unique_ptr<GLProgram> _program;
    std::shared_ptr<VideoRender> _videoRenderImpl;
    const GLfloat* _preferredConversion;
    CVOpenGLESTextureCacheRef _videoTextureCache;
    
    // Textures
    CVOpenGLESTextureRef _lumaTexture;
    CVOpenGLESTextureRef _chromaTexture;
    CVOpenGLESTextureRef _renderTexture;
    
    // Thread safety improvements
    std::atomic<bool> _glContextInitialized;
    std::atomic<bool> _shouldRender;
    std::mutex _pixelBufferMutex;
    std::mutex _renderStateMutex;
    std::atomic<bool> _isDestructing;
}

@property(nonatomic, assign) BOOL isDeallocing;
@property(nonatomic, assign) BOOL isRendering;
@property(nonatomic, strong) CADisplayLink* displayLink;
@property(nonatomic, strong) NSThread* worker;
@property(nonatomic, strong) NSOperationQueue* workQueue;
@property(nonatomic, assign) GLint width;
@property(nonatomic, assign) GLint height;
@property(nonatomic, assign) GLuint frameBuffer;
@property(nonatomic, assign) GLuint renderBuffer;
@property(nonatomic, assign) CVPixelBufferRef pushRenderBuffer;
@property(nonatomic, assign) CVPixelBufferRef preRenderBuffer;

@end

@implementation RenderGLView

+ (Class) layerClass {
    return [CAEAGLLayer class];
}

#pragma mark - life cycle

- (instancetype)initWithFrame:(CGRect)frame {
    if (self = [super initWithFrame:frame]) {
        [self initData];
        [self setupWorkThread];
    }
    return self;
}

- (void)dealloc {
    _isDestructing.store(true);
    _isDeallocing = YES;
    _shouldRender.store(false);
    
    [self stop];
    
    // Clean up worker thread safely
    if (_worker) {
        [self performSelector:@selector(stopRunLoop) onThread:_worker withObject:nil waitUntilDone:YES];
        [_worker cancel];
        _worker = nil;
    }
    
    // Thread-safe pixel buffer cleanup
    {
        std::lock_guard<std::mutex> lock(_pixelBufferMutex);
        if (_pushRenderBuffer) {
            CVPixelBufferRelease(_pushRenderBuffer);
            _pushRenderBuffer = NULL;
        }
        if (_preRenderBuffer) {
            CVPixelBufferRelease(_preRenderBuffer);
            _preRenderBuffer = NULL;
        }
    }
    
    // Clean up OpenGL resources
    [self deleteFrameBuffer];
    [self cleanUpTextures];
    
    if (_videoTextureCache) {
        CFRelease(_videoTextureCache);
        _videoTextureCache = NULL;
    }
    
    // Clean up video render implementation
    _videoRenderImpl.reset();
    _context.reset();
}

- (void)stopRunLoop {
    CFRunLoopStop(CFRunLoopGetCurrent());
}

#pragma mark - Thread Safety Helpers

- (BOOL)isValidForRendering {
    return !_isDestructing.load() && _shouldRender.load() && _glContextInitialized.load();
}

- (void)executeOnRenderThread:(void(^)(void))block waitUntilDone:(BOOL)wait {
    if (_isDestructing.load() || !_worker) {
        return;
    }
    
    __weak __typeof(self) weakSelf = self;
    [self.workQueue addOperationWithBlock:^{
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (strongSelf && !strongSelf->_isDestructing.load()) {
            [strongSelf performSelector:@selector(executeBlock:) 
                               onThread:strongSelf->_worker 
                             withObject:[block copy] 
                          waitUntilDone:wait];
        }
    }];
}

- (void)executeBlock:(void(^)(void))block {
    if (block && !_isDestructing.load()) {
        block();
    }
}

#pragma mark - public
- (void)setContext:(slark::IEGLContextRefPtr) sharecontext {
    _context = sharecontext;
    
    [self executeOnRenderThread:^{
        [self setupGL];
    } waitUntilDone:NO];
}

- (void)start {
    if (_displayLink || _isDestructing.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(_renderStateMutex);
    self.isRendering = NO;
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(render)];
    [_displayLink setPreferredFramesPerSecond:_renderInterval];
    [_displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)stop {
    std::lock_guard<std::mutex> lock(_renderStateMutex);
    if (_displayLink) {
        [_displayLink invalidate];
        _displayLink = nil;
    }
    
    if (!self.isDeallocing) {
        [self executeOnRenderThread:^{
            [self renderGLFinsh];
        } waitUntilDone:NO];
    }
    [_workQueue waitUntilAllOperationsAreFinished];
}

- (CGFloat)scale {
    CGFloat scale = 0;
    if ([[UIScreen mainScreen] respondsToSelector:@selector(nativeScale)]) {
        scale = [UIScreen mainScreen].nativeScale;
    } else {
        scale = [UIScreen mainScreen].scale;
    }
    return scale;
}

- (std::weak_ptr<IVideoRender>)renderImpl {
    return std::weak_ptr<IVideoRender>(_videoRenderImpl);
}

- (void)layoutSubviews {
    [super layoutSubviews];
    [self resizeViewport];
}

#pragma mark - Viewport Management

- (void)resizeViewport {
    if (_isDestructing.load()) {
        return;
    }
    
    CGSize layerSize = self.bounds.size;
    CGFloat scale = [self scale];
    CGFloat width = layerSize.width * scale;
    CGFloat height = layerSize.height * scale;
    
    if (width == self.width && height == self.height) {
        return;
    }
    
    self.width = width;
    self.height = height;
    NSLog(@"size: %d %d", self.width, self.height);
    
    [self executeOnRenderThread:^{
        [self rebuildFrameBuffer];
    } waitUntilDone:NO];
    
    // Handle pre-render buffer
    CVPixelBufferRef buffer = NULL;
    {
        std::lock_guard<std::mutex> lock(_pixelBufferMutex);
        buffer = _preRenderBuffer;
        _preRenderBuffer = NULL;
    }
    
    if (buffer != NULL) {
        [self pushRenderPixelBuffer:buffer];
    }
}

#pragma mark - setter
- (void)setRenderInterval:(NSInteger)renderInterval {
    if (renderInterval != 0 && _renderInterval != renderInterval) {
        _renderInterval = renderInterval;
        if (_displayLink) {
            _displayLink.preferredFramesPerSecond = _renderInterval;
        }
    }
}

#pragma mark - private
- (void)initData {
    _renderInterval = 24; // default 30 fps
    _isRendering = NO;
    _isActive = YES;
    _preferredConversion = kColorConversion709VideoRange;
    
    // Initialize thread-safe state
    _glContextInitialized.store(false);
    _shouldRender.store(true);
    _isDestructing.store(false);
    
    CGSize layerSize = self.layer.bounds.size;
    CGFloat scale = [self scale];
    self.width = layerSize.width * scale;
    self.height = layerSize.height * scale;
    
    // Thread-safe pixel buffer initialization
    {
        std::lock_guard<std::mutex> lock(_pixelBufferMutex);
        _pushRenderBuffer = NULL;
        _preRenderBuffer = NULL;
    }
    
    [self setupRenderDelegate];
    [self setupLayer];
}

- (void)setupRenderDelegate {
    _videoRenderImpl = std::make_shared<VideoRender>();
    __weak __typeof(self) weakSelf = self;
    
    _videoRenderImpl->notifyVideoInfoFunc = [weakSelf](std::shared_ptr<VideoInfo> info) {
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (!strongSelf || strongSelf->_isDestructing.load()) {
            return;
        }
        
        if (info && info->fps > 0) {
            [strongSelf setRenderInterval:info->fps];
        }
    };
    
    _videoRenderImpl->startFunc = [weakSelf]() {
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (!strongSelf || strongSelf->_isDestructing.load()) {
            return;
        }
        strongSelf.isActive = YES;
    };
    
    _videoRenderImpl->pauseFunc = [weakSelf](){
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (!strongSelf || strongSelf->_isDestructing.load()) {
            return;
        }
        strongSelf.isActive = NO;
    };
    
    _videoRenderImpl->pushVideoFrameRenderFunc = [weakSelf](AVFrameRefPtr frame) {
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (!strongSelf || strongSelf->_isDestructing.load()) {
            return;
        }
        if (frame) {
            auto* opaque = frame->opaque;
            frame->opaque = nullptr;
            [strongSelf pushRenderPixelBuffer:opaque];
        }
    };
}


#pragma mark - OpenGL Setup and Management

- (void)setupGL {
    if (!_context || _isDestructing.load()) {
        return;
    }
    
    _context->attachContext();
    _program = std::make_unique<GLProgram>(GLShader::vertexShader, GLShader::fragmentShader);
    if (!_program->isValid()) {
        LogE("program is invalid!");
        return;
    }
    // Get attribute and uniform locations
    auto program = _program->program();
    attributes[ATTRIB_VERTEX] = static_cast<GLuint>(glGetAttribLocation(program, "position"));
    attributes[ATTRIB_TEXCOORD] = static_cast<GLuint>(glGetAttribLocation(program, "texCoord"));
    
    uniforms[UNIFORM_Y] = glGetUniformLocation(program, "SamplerY");
    uniforms[UNIFORM_UV] = glGetUniformLocation(program, "SamplerUV");
    uniforms[UNIFORM_ROTATION_ANGLE] = glGetUniformLocation(program, "preferredRotation");
    uniforms[UNIFORM_COLOR_CONVERSION_MATRIX] = glGetUniformLocation(program, "colorConversionMatrix");
    
    [self createFrameBuffer];
    
    glUseProgram(program);
    glUniform1i(uniforms[UNIFORM_Y], 0);
    glUniform1i(uniforms[UNIFORM_UV], 1);
    glUniform1f(uniforms[UNIFORM_ROTATION_ANGLE], 0);
    glUniformMatrix3fv(uniforms[UNIFORM_COLOR_CONVERSION_MATRIX], 1, GL_FALSE, _preferredConversion);
    glUseProgram(0);
    
    _context->detachContext();
    _glContextInitialized.store(true);
    
    LogI("OpenGL setup completed successfully");
}

- (void)setupLayer {
    CAEAGLLayer* layer = static_cast<CAEAGLLayer*>(self.layer);
    layer.opaque = YES;
    layer.contentsScale = [self scale];
    layer.drawableProperties = [NSDictionary dictionaryWithObjectsAndKeys:
                                [NSNumber numberWithBool:FALSE], kEAGLDrawablePropertyRetainedBacking,
                                kEAGLColorFormatRGBA8, kEAGLDrawablePropertyColorFormat, nil];
}

#pragma mark - Rendering

- (void)renderGLFinsh {
    if (_context && ![self isValidForRendering]) {
        _context->attachContext();
        glFinish();
        _context->detachContext();
    }
}

- (void)render {
    if (self.isRendering || !self.isActive || _isDestructing.load()) {
        return;
    }
    
    self.isRendering = YES;
    [self executeOnRenderThread:^{
        [self doRender:@(NO)];
    } waitUntilDone:NO];
}

- (void)pushRenderPixelBuffer:(void*) buffer {
    if (_isDestructing.load() || !buffer) {
        return;
    }
    
    self.isRendering = YES;
    CVPixelBufferRef pixelBuffer = (CVPixelBufferRef)buffer;
    self.pushRenderBuffer = pixelBuffer;
    CVPixelBufferRelease(pixelBuffer);
    
    [self executeOnRenderThread:^{
        [self doRender:@(YES)];
    } waitUntilDone:NO];
}

- (void)doRender:(NSNumber*)isPushRender {
    if (![self isValidForRendering]) {
        self.isRendering = NO;
        return;
    }
    
    BOOL isPush = [isPushRender boolValue];
    CVPixelBufferRef buffer = NULL;
    
    if (isPush) {
        buffer = CVPixelBufferRetain(self.pushRenderBuffer);
        self.pushRenderBuffer = NULL;
    } else if (_videoRenderImpl) {
        buffer = _videoRenderImpl->requestRender(); //Retained in videotoolbox
    }
    
    if (buffer) {
        [self displayPixelBuffer:buffer];
        self.preRenderBuffer = CVPixelBufferRetain(buffer); //Retained the previous frame
        CVPixelBufferRelease(buffer);
    }
    
    self.isRendering = NO;
}

#pragma mark - Pixel Buffer Display

- (void)displayPixelBuffer:(CVPixelBufferRef)pixelBuffer {
    if (!pixelBuffer || ![self isValidForRendering]) {
        NSLog(@"Invalid pixel buffer or rendering state");
        return;
    }
    
    _context->attachContext();
    auto nativeContext = (__bridge EAGLContext*)_context->nativeContext();
    if (!nativeContext) {
        _context->detachContext();
        return;
    }
    
    auto frameWidth = static_cast<GLint>(CVPixelBufferGetWidth(pixelBuffer));
    auto frameHeight = static_cast<GLint>(CVPixelBufferGetHeight(pixelBuffer));
    
    auto formatType = CVPixelBufferGetPixelFormatType(pixelBuffer);
    BOOL uploadSuccess = NO;
    
    if (formatType == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange ||
        formatType == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
        uploadSuccess = [self uploadYUVTexture:pixelBuffer];
    } else if (formatType == kCVPixelFormatType_32BGRA) {
        uploadSuccess = [self uploadRGBATexture:pixelBuffer];
    } else {
        NSLog(@"Unsupported pixel format: %u", formatType);
    }
    
    if (uploadSuccess) {
        [self renderWithTextures:frameWidth height:frameHeight];
    }
    
    [self cleanUpTextures];
    _context->detachContext();
}

- (BOOL)uploadYUVTexture:(CVPixelBufferRef)pixelBuffer {
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
    
    auto frameWidth = static_cast<GLint>(CVPixelBufferGetWidth(pixelBuffer));
    auto frameHeight = static_cast<GLint>(CVPixelBufferGetHeight(pixelBuffer));
    
    // Upload Y texture (luminance)
    glActiveTexture(GL_TEXTURE0);
    CVReturn err = CVOpenGLESTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, _videoTextureCache, pixelBuffer, NULL,
        GL_TEXTURE_2D, GL_RED_EXT, frameWidth, frameHeight,
        GL_RED_EXT, GL_UNSIGNED_BYTE, 0, &_lumaTexture);
    
    if (err != kCVReturnSuccess) {
        NSLog(@"Error creating Y texture: %d", err);
        return NO;
    }
    
    glBindTexture(CVOpenGLESTextureGetTarget(_lumaTexture), CVOpenGLESTextureGetName(_lumaTexture));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Upload UV texture (chrominance)
    glActiveTexture(GL_TEXTURE1);
    err = CVOpenGLESTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, _videoTextureCache, pixelBuffer, NULL,
        GL_TEXTURE_2D, GL_RG_EXT, frameWidth / 2, frameHeight / 2,
        GL_RG_EXT, GL_UNSIGNED_BYTE, 1, &_chromaTexture);
    
    if (err != kCVReturnSuccess) {
        NSLog(@"Error creating UV texture: %d", err);
        return NO;
    }
    
    glBindTexture(CVOpenGLESTextureGetTarget(_chromaTexture), CVOpenGLESTextureGetName(_chromaTexture));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Update color conversion matrix
    glUniform1f(uniforms[UNIFORM_ROTATION_ANGLE], 0);
    glUniformMatrix3fv(uniforms[UNIFORM_COLOR_CONVERSION_MATRIX], 1, GL_FALSE, _preferredConversion);
    
    return YES;
}

- (BOOL)uploadRGBATexture:(CVPixelBufferRef)pixelBuffer {
    auto frameWidth = static_cast<GLint>(CVPixelBufferGetWidth(pixelBuffer));
    auto frameHeight = static_cast<GLint>(CVPixelBufferGetHeight(pixelBuffer));
    
    glActiveTexture(GL_TEXTURE0);
    CVReturn error = CVOpenGLESTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault, _videoTextureCache, pixelBuffer, NULL,
        GL_TEXTURE_2D, GL_RGBA, frameWidth, frameHeight,
        GL_BGRA, GL_UNSIGNED_BYTE, 0, &_renderTexture);
    
    if (error != kCVReturnSuccess) {
        NSLog(@"Error creating RGBA texture: %d", error);
        return NO;
    }
    
    glBindTexture(CVOpenGLESTextureGetTarget(_renderTexture), CVOpenGLESTextureGetName(_renderTexture));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    return YES;
}

- (void)renderWithTextures:(GLint)frameWidth height:(GLint)frameHeight {
    auto nativeContext = (__bridge EAGLContext*)_context->nativeContext();
    if (!nativeContext) {
        NSLog(@"Native context is null");
        return;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, self.frameBuffer);
    glViewport(0, 0, self.width, self.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Calculate aspect ratio preserving viewport
    CGRect viewBounds = CGRectMake(0, 0, self.width, self.height);
    CGSize contentSize = CGSizeMake(frameWidth, frameHeight);
    CGRect vertexSamplingRect = AVMakeRectWithAspectRatioInsideRect(contentSize, viewBounds);
    
    CGSize normalizedSamplingSize = CGSizeMake(0.0, 0.0);
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
    
    // Vertex data for quad
    GLfloat quadVertexData[] = {
        (GLfloat)(-1 * normalizedSamplingSize.width), (GLfloat)(-1 * normalizedSamplingSize.height),
        (GLfloat)(normalizedSamplingSize.width), (GLfloat)(-1 * normalizedSamplingSize.height),
        (GLfloat)(-1 * normalizedSamplingSize.width), (GLfloat)(normalizedSamplingSize.height),
        (GLfloat)(normalizedSamplingSize.width), (GLfloat)(normalizedSamplingSize.height),
    };
    
    // Texture coordinates
    static const GLfloat quadTextureData[] = {
        0.0, 1.0,
        1.0, 1.0,
        0.0, 0.0,
        1.0, 0.0
    };
    
    // Bind vertex attributes
    glVertexAttribPointer(attributes[ATTRIB_VERTEX], 2, GL_FLOAT, 0, 0, quadVertexData);
    glEnableVertexAttribArray(attributes[ATTRIB_VERTEX]);
    
    glVertexAttribPointer(attributes[ATTRIB_TEXCOORD], 2, GL_FLOAT, 0, 0, quadTextureData);
    glEnableVertexAttribArray(attributes[ATTRIB_TEXCOORD]);
    
    // Draw
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    // Present
    [nativeContext presentRenderbuffer:GL_RENDERBUFFER];
}

#pragma mark - Resource Management

- (void)cleanUpTextures {
    @autoreleasepool {
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
}

#pragma mark - Frame Buffer Management

- (void)createFrameBuffer {
    if (!_context || _isDestructing.load()) {
        return;
    }
    
    auto nativeContext = (__bridge EAGLContext*)_context->nativeContext();
    if (!nativeContext) {
        return;
    }
    
    glDisable(GL_DEPTH_TEST);
    
    glEnableVertexAttribArray(attributes[ATTRIB_VERTEX]);
    glVertexAttribPointer(attributes[ATTRIB_VERTEX], 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), 0);
    
    glEnableVertexAttribArray(attributes[ATTRIB_TEXCOORD]);
    glVertexAttribPointer(attributes[ATTRIB_TEXCOORD], 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), 0);
    
    glGenFramebuffers(1, &_frameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer);
    
    glGenRenderbuffers(1, &_renderBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _renderBuffer);
    
    if (![nativeContext renderbufferStorage:GL_RENDERBUFFER fromDrawable:static_cast<CAEAGLLayer*>(self.layer)]) {
        NSLog(@"Failed to create render buffer storage");
        return;
    }
    
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _renderBuffer);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        NSLog(@"Framebuffer not complete: %u", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }
    
    // Create texture cache if needed
    if (!_videoTextureCache) {
        CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, nativeContext, NULL, &_videoTextureCache);
        if (err != noErr) {
            NSLog(@"Error creating texture cache: %d", err);
        }
    }
}

- (void)rebuildFrameBuffer {
    if (![self isValidForRendering]) {
        return;
    }
    
    auto nativeContext = (__bridge EAGLContext*)_context->nativeContext();
    if (!nativeContext) {
        return;
    }
    
    _context->attachContext();
    
    // Delete existing buffers
    if (_frameBuffer) {
        glDeleteFramebuffers(1, &_frameBuffer);
        _frameBuffer = 0;
    }
    if (_renderBuffer) {
        glDeleteRenderbuffers(1, &_renderBuffer);
        _renderBuffer = 0;
    }
    
    // Recreate buffers
    glGenFramebuffers(1, &_frameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer);
    
    glGenRenderbuffers(1, &_renderBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _renderBuffer);
    
    if (![nativeContext renderbufferStorage:GL_RENDERBUFFER fromDrawable:static_cast<CAEAGLLayer*>(self.layer)]) {
        NSLog(@"Failed to rebuild render buffer storage");
        _context->detachContext();
        return;
    }
    
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _renderBuffer);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        NSLog(@"Framebuffer rebuild failed: %u", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }
    
    _context->detachContext();
}

- (void)deleteFrameBuffer {
    if (!_context) {
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
    
    _context->detachContext();
}

#pragma mark - Worker Thread Management

- (void)setupWorkThread {
    if (_workQueue == nil) {
        _workQueue = [NSOperationQueue new];
        _workQueue.maxConcurrentOperationCount = 1;
        _workQueue.name = @"OpenGL_RenderQueue";
        _workQueue.qualityOfService = NSQualityOfServiceUserInteractive;
    }
    
    if (_worker == nil) {
        _worker = [[NSThread alloc] initWithBlock:^{
            @autoreleasepool {
                CFRunLoopSourceContext sourceContext = {0};
                CFRunLoopSourceRef source = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &sourceContext);
                CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0e10, false);
                CFRelease(source);
            }
        }];
        _worker.name = @"OpenGL_RenderThread";
        _worker.qualityOfService = NSQualityOfServiceUserInteractive;
        [_worker start];
    }
}

#pragma mark - Properties Override

- (void)setFrame:(CGRect)frame {
    [super setFrame:frame];
    [self resizeViewport];
}

- (void)setPushRenderBuffer:(CVPixelBufferRef)pushRenderBuffer {
    std::lock_guard<std::mutex> lock(_pixelBufferMutex);
    if (_pushRenderBuffer == pushRenderBuffer) {
        return;
    }
    if (_pushRenderBuffer) {
        CVPixelBufferRelease(_pushRenderBuffer);
        _pushRenderBuffer = NULL;
    }
    if (pushRenderBuffer) {
        pushRenderBuffer = CVPixelBufferRetain(pushRenderBuffer);
    }
}

- (void)setPreRenderBuffer:(CVPixelBufferRef)preRenderBuffer {
    std::lock_guard<std::mutex> lock(_pixelBufferMutex);
    if (_preRenderBuffer == preRenderBuffer) {
        return;
    }
    if (_preRenderBuffer) {
        CVPixelBufferRelease(_preRenderBuffer);
        _preRenderBuffer = NULL;
    }
    if (preRenderBuffer) {
        _preRenderBuffer = CVPixelBufferRetain(preRenderBuffer);
    }
}

@end 
