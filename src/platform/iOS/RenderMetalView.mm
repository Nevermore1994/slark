//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "RenderMetalView.h"

#include "Clock.h"
#include "VideoInfo.h"
#import "interface/SlarkGraphicsConfig.h"

#import <AVFoundation/AVUtilities.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <atomic>
#import <cmath>
#import <functional>
#import <limits>
#import <memory>
#import <simd/simd.h>

namespace slark {
struct IVideoRender;
struct VideoInfo;
struct AVFrame;
using AVFrameRefPtr = std::shared_ptr<AVFrame>;
} // namespace slark

using namespace slark;

static NSString* const kMetalShaderSource = @R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

struct Uniforms {
    float3x3 colorMatrix;
    float    yOffset;
    float    rotation;
};

vertex VertexOut vertexMain(VertexIn in [[stage_in]],
                            constant Uniforms& u [[buffer(1)]]) {
    float cosR = cos(u.rotation);
    float sinR = sin(u.rotation);
    float2x2 rot = float2x2(float2(cosR, sinR), float2(-sinR, cosR));
    float2 rotated = rot * in.position;
    VertexOut out;
    out.position = float4(rotated, 0.0, 1.0);
    out.texCoord = in.texCoord;
    return out;
}

fragment float4 fragmentYUV(VertexOut in [[stage_in]],
                            constant Uniforms& u [[buffer(1)]],
                            texture2d<float> textureY  [[texture(0)]],
                            texture2d<float> textureUV [[texture(1)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    float3 yuv;
    yuv.x  = textureY.sample(s, in.texCoord).r - u.yOffset;
    yuv.yz = textureUV.sample(s, in.texCoord).rg - 0.5;
    float3 rgb = u.colorMatrix * yuv;
    return float4(rgb, 1.0);
}

fragment float4 fragmentRGBA(VertexOut in [[stage_in]],
                             constant Uniforms& u [[buffer(1)]],
                             texture2d<float> textureBGRA [[texture(0)]]) {
    constexpr sampler s(filter::linear, address::clamp_to_edge);
    return textureBGRA.sample(s, in.texCoord);
}
)";

static const simd_float3x3 kMatrix601VideoRange = {{
    {1.164f, 1.164f, 1.164f},
    {0.0f, -0.392f, 2.017f},
    {1.596f, -0.813f, 0.0f},
}};
static const simd_float3x3 kMatrix601FullRange = {{
    {1.0f, 1.0f, 1.0f},
    {0.0f, -0.344136f, 1.772f},
    {1.402f, -0.714136f, 0.0f},
}};
static const simd_float3x3 kMatrix709VideoRange = {{
    {1.164f, 1.164f, 1.164f},
    {0.0f, -0.213f, 2.112f},
    {1.793f, -0.533f, 0.0f},
}};
static const simd_float3x3 kMatrix709FullRange = {{
    {1.0f, 1.0f, 1.0f},
    {0.0f, -0.187324f, 1.8556f},
    {1.5748f, -0.468124f, 0.0f},
}};

struct MetalUniforms {
    simd_float3x3 colorMatrix;
    float yOffset;
    float rotation;
    float _pad[2];
};

struct MetalVideoRender : public IVideoRender {
    void notifyVideoInfo(std::shared_ptr<VideoInfo> info) noexcept override {
        if (notifyVideoInfoFunc) {
            notifyVideoInfoFunc(info);
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
                LogE("MetalVideoRender requestRender error");
            }
        }
        return nil;
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

    std::function<void(CVPixelBufferRef)> pushFrameFunc;
    std::function<void(std::shared_ptr<VideoInfo>)> notifyVideoInfoFunc;
    std::function<void()> startFunc;
    std::function<void()> pauseFunc;
    std::function<void()> notifyRenderInfoFunc;
    Clock videoClock_;
};

@interface RenderMetalView () {
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    id<MTLRenderPipelineState> _yuvPipeline;
    id<MTLRenderPipelineState> _rgbaPipeline;
    id<MTLBuffer> _vertexBuffer;
    id<MTLBuffer> _texCoordBuffer;
    CVMetalTextureCacheRef _textureCache;

    std::shared_ptr<MetalVideoRender> _videoRenderImpl;
    std::atomic<bool> _setupComplete;
    std::atomic<bool> _isDestructing;
    std::atomic<float> _atomicWidth;
    std::atomic<float> _atomicHeight;

    CVPixelBufferRef _preRenderBuffer;
    double _rotation;
    BOOL _hdrEnabled;
}

@property (atomic, assign) BOOL isActive;
@property (nonatomic, assign, readwrite) BOOL supportsHDR;
@property (nonatomic, assign) NSInteger renderInterval;
@property (nonatomic, strong) CADisplayLink* displayLink;
@property (nonatomic, strong) dispatch_queue_t renderQueue;
@end

@implementation RenderMetalView

@synthesize rotation = _rotation;

+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    return [self initWithFrame:frame hdrEnabled:NO];
}

- (instancetype)initWithFrame:(CGRect)frame hdrEnabled:(BOOL)hdrEnabled {
    if (self = [super initWithFrame:frame]) {
        _hdrEnabled = hdrEnabled;
        [self setupDefaults];
        [self setupRenderQueue];
        [self setupRenderDelegate];
        [self setupLayer];
        dispatch_async(_renderQueue, ^{
            [self setupMetal];
        });
    }
    return self;
}

- (void)dealloc {
    _isDestructing.store(true);
    [self stop];
    if (_renderQueue) {
        dispatch_sync(_renderQueue, ^{
            [self cleanupMetal];
            if (_preRenderBuffer) {
                CVPixelBufferRelease(_preRenderBuffer);
                _preRenderBuffer = NULL;
            }
        });
    }
}

#pragma mark - Setup

- (void)setupDefaults {
    _rotation = 0.0;
    _renderInterval = 30;
    _isActive = YES;
    _setupComplete.store(false);
    _isDestructing.store(false);
    _preRenderBuffer = NULL;

    CGFloat scale = [[UIScreen mainScreen] scale];
    _atomicWidth.store(static_cast<float>(self.bounds.size.width * scale));
    _atomicHeight.store(static_cast<float>(self.bounds.size.height * scale));
}

- (void)setupRenderQueue {
    _renderQueue = dispatch_queue_create("com.slark.metal.render", DISPATCH_QUEUE_SERIAL);
    dispatch_set_target_queue(_renderQueue,
                              dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
}

- (void)setupRenderDelegate {
    _videoRenderImpl = std::make_shared<MetalVideoRender>();
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
    CAMetalLayer* layer = static_cast<CAMetalLayer*>(self.layer);
    layer.contentsScale = [[UIScreen mainScreen] scale];
    layer.opaque = YES;
    if (_hdrEnabled && [SlarkGraphicsConfig isHDRDisplayAvailable]) {
        layer.pixelFormat = MTLPixelFormatRGBA16Float;
        layer.framebufferOnly = YES;
        if (@available(iOS 16.0, *)) {
            layer.wantsExtendedDynamicRangeContent = YES;
        }
        _supportsHDR = YES;
    } else {
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        _supportsHDR = NO;
    }
}

#pragma mark - Metal Initialization

- (void)setupMetal {
    if (_isDestructing.load()) {
        return;
    }
    if (![self createDevice]) {
        return;
    }
    id<MTLLibrary> library = [self compileShaders];
    if (!library) {
        return;
    }
    if (![self buildPipelinesWithLibrary:library]) {
        return;
    }
    [self createBuffers];
    if (![self createTextureCache]) {
        return;
    }
    _setupComplete.store(true);
}

- (BOOL)createDevice {
    _device = MTLCreateSystemDefaultDevice();
    if (!_device) {
        LogE("Metal is not supported on this device");
        return NO;
    }
    dispatch_sync(dispatch_get_main_queue(), ^{
        CAMetalLayer* layer = static_cast<CAMetalLayer*>(self.layer);
        layer.device = _device;
    });
    _commandQueue = [_device newCommandQueue];
    return YES;
}

- (id<MTLLibrary>)compileShaders {
    NSError* error = nil;
    id<MTLLibrary> library = [_device newLibraryWithSource:kMetalShaderSource
                                                   options:nil
                                                     error:&error];
    if (!library) {
        LogE("Failed to compile Metal shaders: {}", [[error localizedDescription] UTF8String]);
    }
    return library;
}

- (MTLVertexDescriptor*)buildVertexDescriptor {
    MTLVertexDescriptor* vd = [MTLVertexDescriptor new];
    vd.attributes[0].format = MTLVertexFormatFloat2;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 0;
    vd.attributes[1].format = MTLVertexFormatFloat2;
    vd.attributes[1].offset = 0;
    vd.attributes[1].bufferIndex = 2;
    vd.layouts[0].stride = sizeof(float) * 2;
    vd.layouts[2].stride = sizeof(float) * 2;
    return vd;
}

- (BOOL)buildPipelinesWithLibrary:(id<MTLLibrary>)library {
    NSError* error = nil;
    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction = [library newFunctionWithName:@"vertexMain"];
    desc.colorAttachments[0].pixelFormat =
        _supportsHDR ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
    desc.vertexDescriptor = [self buildVertexDescriptor];

    desc.fragmentFunction = [library newFunctionWithName:@"fragmentYUV"];
    _yuvPipeline = [_device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!_yuvPipeline) {
        LogE("Failed to create YUV pipeline: {}", [[error localizedDescription] UTF8String]);
        return NO;
    }

    desc.fragmentFunction = [library newFunctionWithName:@"fragmentRGBA"];
    _rgbaPipeline = [_device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!_rgbaPipeline) {
        LogE("Failed to create RGBA pipeline: {}", [[error localizedDescription] UTF8String]);
        return NO;
    }
    return YES;
}

- (void)createBuffers {
    static const float texCoords[] = {0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    _texCoordBuffer = [_device newBufferWithBytes:texCoords
                                           length:sizeof(texCoords)
                                          options:MTLResourceStorageModeShared];
    _vertexBuffer = [_device newBufferWithLength:sizeof(float) * 8
                                         options:MTLResourceStorageModeShared];
}

- (BOOL)createTextureCache {
    CVReturn ret =
        CVMetalTextureCacheCreate(kCFAllocatorDefault, nil, _device, nil, &_textureCache);
    if (ret != kCVReturnSuccess) {
        LogE("CVMetalTextureCacheCreate failed: {}", ret);
        return NO;
    }
    return YES;
}

- (void)cleanupMetal {
    if (_textureCache) {
        CFRelease(_textureCache);
        _textureCache = nil;
    }
    _yuvPipeline = nil;
    _rgbaPipeline = nil;
    _vertexBuffer = nil;
    _texCoordBuffer = nil;
    _commandQueue = nil;
    _device = nil;
}

#pragma mark - Public Interface

- (void)start {
    if (_displayLink) {
        return;
    }
    _displayLink = [CADisplayLink displayLinkWithTarget:self
                                               selector:@selector(displayLinkCallback)];
    _displayLink.preferredFramesPerSecond = _renderInterval;
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
    @synchronized(self) {
        _rotation = rotation * kDegreesToRadians;
    }
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
    float newW = static_cast<float>(self.bounds.size.width * scale);
    float newH = static_cast<float>(self.bounds.size.height * scale);

    if (std::abs(newW - _atomicWidth.load()) > std::numeric_limits<float>::epsilon() ||
        std::abs(newH - _atomicHeight.load()) > std::numeric_limits<float>::epsilon()) {
        _atomicWidth.store(newW);
        _atomicHeight.store(newH);

        CAMetalLayer* layer = static_cast<CAMetalLayer*>(self.layer);
        layer.drawableSize = CGSizeMake(newW, newH);

        dispatch_async(_renderQueue, ^{
            [self renderPreBufferIfNeeded];
        });
    }
}

#pragma mark - Rendering

- (void)displayLinkCallback {
    if (!_isActive || _isDestructing.load()) {
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

#pragma mark - Pixel Buffer Display

- (void)updateVertexBufferWithFrameWidth:(size_t)frameWidth frameHeight:(size_t)frameHeight {
    float viewW = _atomicWidth.load();
    float viewH = _atomicHeight.load();
    CGRect fitRect = AVMakeRectWithAspectRatioInsideRect(CGSizeMake(frameWidth, frameHeight),
                                                         CGRectMake(0, 0, viewW, viewH));
    float normW = static_cast<float>(fitRect.size.width / viewW);
    float normH = static_cast<float>(fitRect.size.height / viewH);
    float vertices[] = {-normW, -normH, normW, -normH, -normW, normH, normW, normH};
    memcpy([_vertexBuffer contents], vertices, sizeof(vertices));
}

- (id<MTLRenderCommandEncoder>)beginRenderPassWithCommandBuffer:(id<MTLCommandBuffer>)cmdBuf
                                                       drawable:(id<CAMetalDrawable>)drawable {
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor new];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    return [cmdBuf renderCommandEncoderWithDescriptor:pass];
}

- (void)displayPixelBuffer:(CVPixelBufferRef)pixelBuffer {
    if (!pixelBuffer || !_setupComplete.load()) {
        return;
    }
    CAMetalLayer* layer = static_cast<CAMetalLayer*>(self.layer);
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable) {
        return;
    }

    auto formatType = CVPixelBufferGetPixelFormatType(pixelBuffer);
    auto frameWidth = static_cast<size_t>(CVPixelBufferGetWidth(pixelBuffer));
    auto frameHeight = static_cast<size_t>(CVPixelBufferGetHeight(pixelBuffer));

    [self updateVertexBufferWithFrameWidth:frameWidth frameHeight:frameHeight];

    id<MTLCommandBuffer> cmdBuf = [_commandQueue commandBuffer];
    id<MTLRenderCommandEncoder> enc = [self beginRenderPassWithCommandBuffer:cmdBuf
                                                                    drawable:drawable];
    MetalUniforms uniforms{};
    @synchronized(self) {
        uniforms.rotation = static_cast<float>(_rotation);
    }

    if (formatType == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange ||
        formatType == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
        [self encodeYUV:enc
            pixelBuffer:pixelBuffer
             frameWidth:frameWidth
            frameHeight:frameHeight
               uniforms:uniforms
                 cmdBuf:cmdBuf
               drawable:drawable];
    } else if (formatType == kCVPixelFormatType_32BGRA) {
        [self encodeRGBA:enc
             pixelBuffer:pixelBuffer
              frameWidth:frameWidth
             frameHeight:frameHeight
                uniforms:uniforms
                  cmdBuf:cmdBuf
                drawable:drawable];
    } else {
        [enc endEncoding];
        [cmdBuf commit];
    }
    CVMetalTextureCacheFlush(_textureCache, 0);
}

- (MetalUniforms)buildYUVUniformsForPixelBuffer:(CVPixelBufferRef)pixelBuffer
                                   baseUniforms:(MetalUniforms)uniforms {
    bool isFullRange = (CVPixelBufferGetPixelFormatType(pixelBuffer) ==
                        kCVPixelFormatType_420YpCbCr8BiPlanarFullRange);
    uniforms.yOffset = isFullRange ? 0.0f : (16.0f / 255.0f);

    CFTypeRef colorAtt = CVBufferCopyAttachment(pixelBuffer, kCVImageBufferYCbCrMatrixKey, NULL);
    bool is601 = colorAtt && CFStringCompare(static_cast<CFStringRef>(colorAtt),
                                             kCVImageBufferYCbCrMatrix_ITU_R_601_4,
                                             0) == kCFCompareEqualTo;
    if (colorAtt) {
        CFRelease(colorAtt);
    }

    uniforms.colorMatrix = is601 ? (isFullRange ? kMatrix601FullRange : kMatrix601VideoRange)
                                 : (isFullRange ? kMatrix709FullRange : kMatrix709VideoRange);
    return uniforms;
}

- (BOOL)createYUVTexturesFromPixelBuffer:(CVPixelBufferRef)pixelBuffer
                              frameWidth:(size_t)frameWidth
                             frameHeight:(size_t)frameHeight
                                 outTexY:(CVMetalTextureRef*)outTexY
                                outTexUV:(CVMetalTextureRef*)outTexUV {
    CVReturn ret = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                             _textureCache,
                                                             pixelBuffer,
                                                             nil,
                                                             MTLPixelFormatR8Unorm,
                                                             frameWidth,
                                                             frameHeight,
                                                             0,
                                                             outTexY);
    if (ret != kCVReturnSuccess) {
        return NO;
    }

    ret = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                    _textureCache,
                                                    pixelBuffer,
                                                    nil,
                                                    MTLPixelFormatRG8Unorm,
                                                    frameWidth / 2,
                                                    frameHeight / 2,
                                                    1,
                                                    outTexUV);
    if (ret != kCVReturnSuccess) {
        CFRelease(*outTexY);
        *outTexY = nil;
        return NO;
    }
    return YES;
}

- (void)encodeYUV:(id<MTLRenderCommandEncoder>)enc
      pixelBuffer:(CVPixelBufferRef)pixelBuffer
       frameWidth:(size_t)frameWidth
      frameHeight:(size_t)frameHeight
         uniforms:(MetalUniforms)uniforms
           cmdBuf:(id<MTLCommandBuffer>)cmdBuf
         drawable:(id<CAMetalDrawable>)drawable {
    uniforms = [self buildYUVUniformsForPixelBuffer:pixelBuffer baseUniforms:uniforms];

    CVMetalTextureRef cvTexY = nil, cvTexUV = nil;
    if (![self createYUVTexturesFromPixelBuffer:pixelBuffer
                                     frameWidth:frameWidth
                                    frameHeight:frameHeight
                                        outTexY:&cvTexY
                                       outTexUV:&cvTexUV]) {
        [enc endEncoding];
        [cmdBuf commit];
        return;
    }

    [enc setRenderPipelineState:_yuvPipeline];
    [enc setVertexBuffer:_vertexBuffer offset:0 atIndex:0];
    [enc setVertexBuffer:_texCoordBuffer offset:0 atIndex:2];
    [enc setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [enc setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [enc setFragmentTexture:CVMetalTextureGetTexture(cvTexY) atIndex:0];
    [enc setFragmentTexture:CVMetalTextureGetTexture(cvTexUV) atIndex:1];
    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [enc endEncoding];
    [cmdBuf presentDrawable:drawable];
    [cmdBuf commit];

    CFRelease(cvTexY);
    CFRelease(cvTexUV);
}

- (void)encodeRGBA:(id<MTLRenderCommandEncoder>)enc
       pixelBuffer:(CVPixelBufferRef)pixelBuffer
        frameWidth:(size_t)frameWidth
       frameHeight:(size_t)frameHeight
          uniforms:(MetalUniforms)uniforms
            cmdBuf:(id<MTLCommandBuffer>)cmdBuf
          drawable:(id<CAMetalDrawable>)drawable {
    CVMetalTextureRef cvTex = nil;
    CVReturn ret = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                             _textureCache,
                                                             pixelBuffer,
                                                             nil,
                                                             MTLPixelFormatBGRA8Unorm,
                                                             frameWidth,
                                                             frameHeight,
                                                             0,
                                                             &cvTex);
    if (ret != kCVReturnSuccess) {
        [enc endEncoding];
        [cmdBuf commit];
        return;
    }

    [enc setRenderPipelineState:_rgbaPipeline];
    [enc setVertexBuffer:_vertexBuffer offset:0 atIndex:0];
    [enc setVertexBuffer:_texCoordBuffer offset:0 atIndex:2];
    [enc setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [enc setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [enc setFragmentTexture:CVMetalTextureGetTexture(cvTex) atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [enc endEncoding];

    [cmdBuf presentDrawable:drawable];
    [cmdBuf commit];

    CFRelease(cvTex);
}

@end
