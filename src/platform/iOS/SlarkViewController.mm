
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//

#import "interface/SlarkViewController.h"

#import "GLContextManager.h"
#import "IRenderView.h"
#import "Player.h"
#import "RenderGLView.h"
#import "RenderMetalView.h"
#import "interface/SlarkGraphicsConfig.h"

#import <Foundation/Foundation.h>

@interface SlarkPlayer (Render)
- (void)setRenderImpl:(void*)ptr;
- (void)setRotationCallback:(void (^)(double))callback;
@end

@interface SlarkViewController ()
@property (nonatomic, strong) UIView<IRenderView>* renderView;
@property (nonatomic, strong) SlarkPlayer* player;
@property (nonatomic, assign) BOOL isActive;
@property (nonatomic, assign) SlarkGraphicsBackend currentBackend;
@end

@implementation SlarkViewController

- (instancetype)initWithPlayer:(CGRect)frame player:(SlarkPlayer*)player {
    if (self = [self init]) {
        self.player = player;
#if TARGET_IPHONE_SIMULATOR
        self.currentBackend = SlarkGraphicsBackendMetal;
#else
        self.currentBackend = SlarkGraphicsBackendOpenGLES;
#endif
        self.renderView = [self createRenderViewWithFrame:frame
                                                  backend:self.currentBackend
                                               hdrEnabled:NO];
        [self.player setRenderImpl:[self.renderView renderImpl]];
        __weak __typeof(self) weakSelf = self;
        [self.player setRotationCallback:^(double degrees) {
            __strong __typeof(weakSelf) strongSelf = weakSelf;
            if (strongSelf) {
                strongSelf.renderView.rotation = degrees;
            }
        }];
        self.view = self.renderView;
        [self setupNotifications];
    }
    return self;
}

- (UIView<IRenderView>*)createRenderViewWithFrame:(CGRect)frame
                                          backend:(SlarkGraphicsBackend)backend
                                       hdrEnabled:(BOOL)hdrEnabled {
#if TARGET_IPHONE_SIMULATOR
    // 模拟器不支持 OpenGLES，直接使用 Metal
    (void)backend;
    return [[RenderMetalView alloc] initWithFrame:frame hdrEnabled:hdrEnabled];
#else
    switch (backend) {
        case SlarkGraphicsBackendMetal:
            return [[RenderMetalView alloc] initWithFrame:frame hdrEnabled:hdrEnabled];
        case SlarkGraphicsBackendOpenGLES:
        default:
            return [[RenderGLView alloc] initWithFrame:frame];
    }
#endif
}

- (void)setupNotifications {
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(deviceOrientationDidChange:)
                                                 name:UIDeviceOrientationDidChangeNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationWillResignActive)
                                                 name:UIApplicationWillResignActiveNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationDidBecomeActive)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
}

- (void)deviceOrientationDidChange:(NSNotification*)notification {
    UIDeviceOrientation orientation = [UIDevice currentDevice].orientation;
    if (orientation == UIDeviceOrientationPortrait ||
        orientation == UIDeviceOrientationPortraitUpsideDown) {
    } else if (orientation == UIDeviceOrientationLandscapeLeft ||
               orientation == UIDeviceOrientationLandscapeRight) {
    }
}

- (void)applicationWillResignActive {
    if (self.renderView.isActive) {
        [self.player pause];
        _isActive = YES;
    } else {
        _isActive = NO;
    }
}

- (void)applicationDidBecomeActive {
    if (_isActive) {
        [self.player play];
    }
    _isActive = NO;
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    [self.renderView start];
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [self.renderView stop];
}

- (void)switchGraphicsBackend:(SlarkGraphicsBackend)backend {
    [self switchGraphicsBackend:backend hdrEnabled:NO];
}

- (void)switchGraphicsBackend:(SlarkGraphicsBackend)backend hdrEnabled:(BOOL)hdrEnabled {
    if (backend == self.currentBackend) {
        return;
    }
    [self.renderView stop];
    CGRect frame = self.renderView.frame;
    UIView<IRenderView>* newView = [self createRenderViewWithFrame:frame
                                                           backend:backend
                                                        hdrEnabled:hdrEnabled];
    [self.player setRenderImpl:[newView renderImpl]];
    __weak __typeof(self) weakSelf = self;
    [self.player setRotationCallback:^(double degrees) {
        __strong __typeof(weakSelf) strongSelf = weakSelf;
        if (strongSelf) {
            strongSelf.renderView.rotation = degrees;
        }
    }];
    self.renderView = newView;
    self.view = newView;
    self.currentBackend = backend;
    [newView start];
}

- (void)dealloc {
    [self.player stop];
    [self.renderView stop];
}

@end
