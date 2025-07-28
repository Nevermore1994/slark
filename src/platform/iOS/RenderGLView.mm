//
//  RenderGLView.cpp
//  slark
//
//  Created by Nevermore on 2024/10/30.
//

#import <AVFoundation/AVUtilities.h>
#import <OpenGLES/ES2/gl.h>
#import <OpenGLES/ES2/glext.h>
#import "RenderGLView.h"
#import "Log.hpp"
#import "GLProgram.h"
#import "GLShader.h"
#import "VideoInfo.h"

using namespace slark;
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
        if (pushVideoFrameRenderFunc) {
            pushVideoFrameRenderFunc(frame);
        }
    }
    
    void renderEnd() noexcept override {
        VideoRender::pause();
    }
    
    ~VideoRender() override = default;
    
    CVPixelBufferRef requestRender() noexcept {
        if (auto requestRenderFunc = requestRenderFunc_.load()) {
            auto frame = std::invoke(*requestRenderFunc);
            if (frame) {
                auto* opaque = frame->opaque;
                frame->opaque = nullptr;
                return static_cast<CVPixelBufferRef>(opaque);
            }
        }
        return nil;
    }
    
    std::function<void(std::shared_ptr<VideoInfo> videoInfo)> notifyVideoInfoFunc;
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

// BT.601
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

// BT.709
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
    //yuv
    CVOpenGLESTextureRef _lumaTexture;
    CVOpenGLESTextureRef _chromaTexture;
    //RGBA
    CVOpenGLESTextureRef _renderTexture;
    CVPixelBufferRef _pixelbuffer;
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
    _isDeallocing = YES;
    [self stop];
    if (_worker) {
        [self performSelector:@selector(stopRunLoop) onThread:_worker withObject:nil waitUntilDone:YES];
        [_worker cancel];
        _worker = nil;
    }
    if (_pixelbuffer) {
        CVPixelBufferRelease(_pixelbuffer);
        _pixelbuffer = NULL;
    }
    [self deleteFrameBuffer];
    [self cleanUpTextures];
    if (_videoTextureCache) {
        CFRelease(_videoTextureCache);
        _videoTextureCache = NULL;
    }
    if (_videoRenderImpl) {
        _videoRenderImpl->notifyVideoInfoFunc = nullptr;
        _videoRenderImpl->startFunc = nullptr;
        _videoRenderImpl->pauseFunc = nullptr;
        _videoRenderImpl->pushVideoFrameRenderFunc = nullptr;
        _videoRenderImpl.reset();
    }
    _context.reset();
}

- (void)stopRunLoop {
    CFRunLoopStop(CFRunLoopGetCurrent());
}

#pragma mark - public
- (void)setContext:(slark::IEGLContextRefPtr) sharecontext {
    _context = sharecontext;
    [self setupGL];
}

- (void)start {
    if (_displayLink) {
        return;
    }
    self.isRendering = NO;
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(render)];
    [_displayLink setPreferredFramesPerSecond:_renderInterval];
    [_displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)stop {
    if (_displayLink) {
        [_displayLink invalidate];
        _displayLink = nil;
    }
    if (!self.isDeallocing) {
        __weak __typeof(self) weakSelf = self;
        [self.workQueue addOperationWithBlock:^{
            __strong __typeof(weakSelf) strongSelf = weakSelf;
            [strongSelf performSelector:@selector(renderGLFinsh) onThread:strongSelf->_worker withObject:nil waitUntilDone:YES];
        }];
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

#pragma mark - setter
- (void)setRenderInterval:(NSInteger)renderInterval {
    if (renderInterval != 0 && _renderInterval != renderInterval) {
        _renderInterval = renderInterval;
        _displayLink.preferredFramesPerSecond = _renderInterval;
    }
}

#pragma mark - private
- (void)initData {
    _renderInterval = 24; //default 30 fps
    _isRendering = NO;
    _isActive = YES;
    _preferredConversion = kColorConversion709VideoRange;
    _pixelbuffer = NULL;
    [self setupRenderDelegate];
    [self setupLayer];
}

- (void)setupRenderDelegate{
    _videoRenderImpl = std::make_shared<VideoRender>();
    __weak __typeof(self) weakSelf = self;
    _videoRenderImpl->notifyVideoInfoFunc = [weakSelf](std::shared_ptr<VideoInfo> info) {
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (!strongSelf) {
            return;
        }
        [strongSelf setRenderInterval:info->fps];
    };
    _videoRenderImpl->startFunc = [weakSelf]() {
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (!strongSelf) {
            return;
        }
        strongSelf.isActive = YES;
    };
    _videoRenderImpl->pauseFunc = [weakSelf](){
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (!strongSelf) {
            return;
        }
        strongSelf.isActive = NO;
    };
    _videoRenderImpl->pushVideoFrameRenderFunc = [weakSelf](AVFrameRefPtr frame) {
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (!strongSelf) {
            return;
        }
        if (frame) {
            auto* opaque = frame->opaque;
            frame->opaque = nullptr;
            [strongSelf pushRenderPixelBuffer:opaque];
        }
    };
}

- (void)setupGL {
    _context->attachContext();
    _program = std::make_unique<GLProgram>(GLShader::vertexShader, GLShader::fragmentShader);
    if (!_program->isValid()) {
        LogE("setupGL error");
        return;
    }
    GLuint program = _program->program();
    attributes[ATTRIB_VERTEX] = static_cast<GLuint>(glGetAttribLocation(program, "position"));
    attributes[ATTRIB_TEXCOORD] =  static_cast<GLuint>(glGetAttribLocation(program, "texCoord"));
    //Y亮度纹理
    uniforms[UNIFORM_Y] = glGetUniformLocation(program, "SamplerY");
    //UV色量纹理
    uniforms[UNIFORM_UV] = glGetUniformLocation(program, "SamplerUV");
    //旋转角度preferredRotation
    uniforms[UNIFORM_ROTATION_ANGLE] = glGetUniformLocation(program, "preferredRotation");
    //YUV->RGB
    uniforms[UNIFORM_COLOR_CONVERSION_MATRIX] = glGetUniformLocation(program, "colorConversionMatrix");
    
    [self createFrameBuffer];
    _program->attach();
    
    glUniform1i(uniforms[UNIFORM_Y], 0);
    glUniform1i(uniforms[UNIFORM_UV], 1);
    glUniform1f(uniforms[UNIFORM_ROTATION_ANGLE], 0);

    glUniformMatrix3fv(uniforms[UNIFORM_COLOR_CONVERSION_MATRIX], 1, GL_FALSE, _preferredConversion);
    _program->detach();
    _context->detachContext();
}

- (void)setupLayer {
    CAEAGLLayer* layer = static_cast<CAEAGLLayer*>(self.layer);
    layer.opaque = YES;
    layer.contentsScale = [self scale];
    layer.drawableProperties = [NSDictionary dictionaryWithObjectsAndKeys:
                                [NSNumber numberWithBool:FALSE],kEAGLDrawablePropertyRetainedBacking,
                                kEAGLColorFormatRGBA8, kEAGLDrawablePropertyColorFormat, nil];
}

- (void)renderGLFinsh {
    if (_context) {
        _context->attachContext();
        glFinish();
        _context->detachContext();
    }
}

- (void)render {
    if (self.isRendering || !self.isActive) {
        return;
    }
    self.isRendering = YES;
    __weak __typeof(self) weakSelf = self;
    [self.workQueue addOperationWithBlock:^{
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        [strongSelf performSelector:@selector(doRender:) onThread:strongSelf->_worker withObject:@(NO) waitUntilDone:YES];
    }];
}

- (void)pushRenderPixelBuffer:(void*) buffer{
    self.isRendering = YES;
    _pixelbuffer = (CVPixelBufferRef)buffer;
    __weak __typeof(self) weakSelf = self;
    [self.workQueue addOperationWithBlock:^{
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        [strongSelf performSelector:@selector(doRender:) onThread:strongSelf->_worker withObject:@(YES) waitUntilDone:YES];
    }];
}

//yuv
- (void)displayPixelBuffer:(CVPixelBufferRef)pixelBuffer
{
    if(pixelBuffer == NULL) {
        LogE("Pixel buffer is null");
        return;
    }
    
    if (!_context) {
        CVPixelBufferRelease(pixelBuffer);
        return;
    }
    _context->attachContext();
    auto nativeContext = (__bridge EAGLContext*)_context->nativeContext();
    if (!nativeContext) {
        CVPixelBufferRelease(pixelBuffer);
        return;
    }
    auto frameWidth = static_cast<GLint>(CVPixelBufferGetWidth(pixelBuffer));
    auto frameHeight = static_cast<GLint>(CVPixelBufferGetHeight(pixelBuffer));
    
    auto formatType = CVPixelBufferGetPixelFormatType(pixelBuffer);
    if (formatType == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange ||
        formatType == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
        [self uploadYUVTexture:pixelBuffer];
    } else if (formatType == kCVPixelFormatType_32BGRA) {
        [self uploadRGBATexture:pixelBuffer];
    } else {
        LogE("Not support current format.");
        CVPixelBufferRelease(pixelBuffer);
        return;
    }
    
    [self renderWithTextures:frameWidth height:frameHeight];
    CVPixelBufferRelease(pixelBuffer);
    [self cleanUpTextures];
}

- (BOOL)uploadYUVTexture:(CVPixelBufferRef)pixelBuffer {
    CFTypeRef colorAttachments = CVBufferCopyAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, NULL);
    if (CFStringCompare(static_cast<CFStringRef>(colorAttachments), kCVImageBufferYCbCrMatrix_ITU_R_601_4, 0) == kCFCompareEqualTo) {
        _preferredConversion = kColorConversion601VideoRange;
    } else {
        _preferredConversion = kColorConversion709VideoRange;
    }
    CFRelease(colorAttachments);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
    auto frameWidth = static_cast<GLint>(CVPixelBufferGetWidth(pixelBuffer));
    auto frameHeight = static_cast<GLint>(CVPixelBufferGetHeight(pixelBuffer));
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
    if (err) {
        LogE("Error at CVOpenGLESTextureCacheCreateTextureFromImage:{}", err);
        return NO;
    }
    
    glBindTexture(CVOpenGLESTextureGetTarget(_lumaTexture), CVOpenGLESTextureGetName(_lumaTexture));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glActiveTexture(GL_TEXTURE1);
    err = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                           _videoTextureCache,
                                                           pixelBuffer,
                                                           NULL,
                                                           GL_TEXTURE_2D,
                                                           GL_RG_EXT,
                                                           frameWidth / 2,
                                                           frameHeight / 2,
                                                           GL_RG_EXT,
                                                           GL_UNSIGNED_BYTE,
                                                           1,
                                                           &_chromaTexture);
    if (err) {
        LogE("Error at CVOpenGLESTextureCacheCreateTextureFromImage {}", err);
        return NO;
    }
        
    glBindTexture(CVOpenGLESTextureGetTarget(_chromaTexture), CVOpenGLESTextureGetName(_chromaTexture));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    _program->attach();
    glUniform1f(uniforms[UNIFORM_ROTATION_ANGLE], 0);
    glUniformMatrix3fv(uniforms[UNIFORM_COLOR_CONVERSION_MATRIX], 1, GL_FALSE, _preferredConversion);
    return YES;
}

- (BOOL)uploadRGBATexture:(CVPixelBufferRef)pixelBuffer {
    auto frameWidth = static_cast<GLint>(CVPixelBufferGetWidth(pixelBuffer));
    auto frameHeight = static_cast<GLint>(CVPixelBufferGetHeight(pixelBuffer));
    glActiveTexture(GL_TEXTURE0);
    CVReturn error = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
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
    
    
    glBindTexture(CVOpenGLESTextureGetTarget(_renderTexture), CVOpenGLESTextureGetName(_renderTexture));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

- (void)renderWithTextures:(GLint)frameWidth height:(GLint)frameHeight {
    auto nativeContext = (__bridge EAGLContext*)_context->nativeContext();
    if (!nativeContext) {
        LogE("nativeContext is nullptr.");
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, self.frameBuffer);
    glViewport(0, 0, self.width, self.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    CGRect viewBounds = self.bounds;
    CGSize contentSize = CGSizeMake(frameWidth, frameHeight);
    
    CGRect vertexSamplingRect = AVMakeRectWithAspectRatioInsideRect(contentSize, viewBounds);
    
    CGSize normalizedSamplingSize = CGSizeMake(0.0, 0.0);
 
    CGSize cropScaleAmount = CGSizeMake(vertexSamplingRect.size.width/viewBounds.size.width,vertexSamplingRect.size.height/viewBounds.size.height);
    
    if (cropScaleAmount.width > cropScaleAmount.height) {
        normalizedSamplingSize.width = 1.0;
        normalizedSamplingSize.height = cropScaleAmount.height/cropScaleAmount.width;
    }
    else {
        normalizedSamplingSize.width = cropScaleAmount.width/cropScaleAmount.height;
        normalizedSamplingSize.height = 1.0;
    }
    
    GLfloat quadVertexData [] = {
        (GLfloat)(-1 * normalizedSamplingSize.width), (GLfloat)(-1 * normalizedSamplingSize.height),
        (GLfloat)(normalizedSamplingSize.width), (GLfloat)(-1 * normalizedSamplingSize.height),
        (GLfloat)(-1 * normalizedSamplingSize.width), (GLfloat)(normalizedSamplingSize.height),
        (GLfloat)(normalizedSamplingSize.width), (GLfloat)(normalizedSamplingSize.height),
    };
    
    glVertexAttribPointer(attributes[ATTRIB_VERTEX], 2, GL_FLOAT, 0, 0, quadVertexData);
    glEnableVertexAttribArray(attributes[ATTRIB_VERTEX]);
    
    static const GLfloat quadTextureData[] = {
        0.0, 1.0,
        1.0, 1.0,
        0.0, 0.0,
        1.0, 0.0
    };
    
    glVertexAttribPointer(attributes[ATTRIB_TEXCOORD], 2, GL_FLOAT, 0, 0, quadTextureData);
    glEnableVertexAttribArray(attributes[ATTRIB_TEXCOORD]);
    
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    glBindRenderbuffer(GL_RENDERBUFFER, _renderBuffer);
    [nativeContext presentRenderbuffer:GL_RENDERBUFFER];
}

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
        CVOpenGLESTextureCacheFlush(_videoTextureCache, 0);
    }
}

- (void)setFrame:(CGRect)frame {
    [super setFrame:frame];
    //update render rect
}

- (void)doRender:(id) isPushRender{
    BOOL isPush = [isPushRender boolValue];
    CVPixelBufferRef buffer = NULL;
    if (isPush) {
        buffer = _pixelbuffer;
        _pixelbuffer = NULL;
    } else if (_videoRenderImpl) {
        buffer = _videoRenderImpl->requestRender();
    }
    if (buffer) {
        [self displayPixelBuffer:buffer];
    }
    self.isRendering = NO;
}

- (void)setupWorkThread {
    if (_workQueue == nil) {
        _workQueue = [NSOperationQueue new];
        _workQueue.maxConcurrentOperationCount = 1;
        _workQueue.name = @"renderQueue";
    }
    if (_worker == nil) {
        _worker = [[NSThread alloc] initWithBlock:^{
                   CFRunLoopSourceContext sourceContext = {0};
                   CFRunLoopSourceRef source = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &sourceContext);
                   CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
                   CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0e10, false);
                   CFRelease(source);
                   }];
        _worker.name = @"renderThread";
        [_worker start];
    }
}

- (void)createFrameBuffer {
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
        LogE("renderbufferStorage error!");
        return;
    }
    
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &_width);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &_height);
    
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _renderBuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LogE("create frame error:{}", glCheckFramebufferStatus(GL_FRAMEBUFFER));
    }
    
    if (!_videoTextureCache) {
        CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, nativeContext, NULL, &_videoTextureCache);
        if (err != noErr) {
            LogE("Error at CVOpenGLESTextureCacheCreate {}", err);
        }
    }
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
@end
