
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "interface/SlarkViewController.h"

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#import "GLContextManager.h"
#import "IRenderView.h"
#import "Player.h"
#import "RenderGLView.h"
#import "RenderMetalView.h"
#import "interface/SlarkGraphicsConfig.h"

@interface SlarkPlayer (Render)
- (void)setRenderImpl:(void*)ptr;
- (void)setRotationCallback:(void (^)(double))callback;
@end

@interface SlarkViewController ()
@property(nonatomic, strong) UIView<IRenderView>* renderView;
@property(nonatomic, strong) SlarkPlayer* player;
@property(nonatomic, assign) BOOL isActive;
@property(nonatomic, assign) SlarkGraphicsBackend currentBackend;
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
    // The simulator does not support OpenGLES; use Metal directly
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
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(audioSessionInterruption:)
                                                 name:AVAudioSessionInterruptionNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(audioSessionRouteChange:)
                                                 name:AVAudioSessionRouteChangeNotification
                                               object:nil];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
}

- (void)deviceOrientationDidChange:(NSNotification*)notification {
    // Screen rotation drives the framebuffer rebuild via layoutSubviews; nothing to do here
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

- (void)audioSessionInterruption:(NSNotification*)notification {
    NSDictionary* info = notification.userInfo;
    AVAudioSessionInterruptionType type = static_cast<AVAudioSessionInterruptionType>(
        [info[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue]);
    if (type == AVAudioSessionInterruptionTypeBegan) {
        // Interruption began (call, VoIP, etc.), pause playback
        if (self.renderView.isActive) {
            [self.player pause];
            _isActive = YES;
        }
    } else if (type == AVAudioSessionInterruptionTypeEnded) {
        // Interruption ended, check whether playback should resume
        NSUInteger options = [info[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue];
        if ((options & AVAudioSessionInterruptionOptionShouldResume) && _isActive) {
            [self.player play];
        }
        _isActive = NO;
    }
}

- (void)audioSessionRouteChange:(NSNotification*)notification {
    NSDictionary* info = notification.userInfo;
    AVAudioSessionRouteChangeReason reason = static_cast<AVAudioSessionRouteChangeReason>(
        [info[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue]);
    // Pause when headphones are unplugged to prevent audio leaking out loud
    if (reason == AVAudioSessionRouteChangeReasonOldDeviceUnavailable) {
        [self.player pause];
    }
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
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [self.player stop];
    [self.renderView stop];
}

@end
