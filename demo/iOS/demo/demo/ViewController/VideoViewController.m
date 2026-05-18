//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import "VideoViewController.h"

#import "EXTScope.h"
#import "Masonry.h"
#import "PlayerControllerView.h"
#import "SlarkGraphicsConfig.h"
#import "SlarkViewController.h"
#import "iOSUtil.h"

#import <AVFoundation/AVFoundation.h>

@interface VideoViewController () <UIGestureRecognizerDelegate, ISlarkPlayerObserver>
@property (nonatomic, strong) PlayerControllerView* controllerView;
@property (nonatomic, strong) SlarkViewController* playerController;
@property (nonatomic, strong) UIImageView* loadingView;
@property (nonatomic, strong) NSTimer* hideControllerTimer;
@property (nonatomic, assign) NSInteger rotationAngle;
@end

@implementation VideoViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    _rotationAngle = 0;
    [self initData];
    [self initSubviews];
    UITapGestureRecognizer* tap =
        [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(handleTap:)];
    [self.view addGestureRecognizer:tap];
    [self resetHideControllerTimer];
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    self.navigationController.interactivePopGestureRecognizer.delegate = self;
    [self setupAudioSession];
}

- (void)dealloc {
    [self.hideControllerTimer invalidate];
    [self.playerController removeFromParentViewController];
}

- (void)setupAudioSession {
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSError* error = nil;
    [session setCategory:AVAudioSessionCategoryPlayback error:&error];
    [session setActive:YES error:&error];
}

- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer*)gestureRecognizer {
    return NO;
}

- (void)handleTap:(UITapGestureRecognizer*)gesture {
    self.controllerView.hidden = !self.controllerView.hidden;
    if (!self.controllerView.hidden) {
        [self resetHideControllerTimer];
    }
}

- (void)resetHideControllerTimer {
    [self.hideControllerTimer invalidate];
    self.hideControllerTimer =
        [NSTimer scheduledTimerWithTimeInterval:5.0
                                         target:self
                                       selector:@selector(dismissControllerView)
                                       userInfo:nil
                                        repeats:NO];
}

- (void)dismissControllerView {
    self.controllerView.hidden = YES;
}

- (void)initSubviews {
    self.navigationController.navigationBar.hidden = YES;
    self.view.backgroundColor = [UIColor blackColor];

    [self.view addSubview:self.playerController.view];
    [self.view addSubview:self.loadingView];
    [self.view addSubview:self.controllerView];

    [self.playerController.view mas_makeConstraints:^(MASConstraintMaker* make) {
        make.edges.equalTo(self.view);
    }];
    [self.loadingView mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.height.mas_equalTo(54);
        make.center.equalTo(self.view);
    }];
    [self.controllerView mas_makeConstraints:^(MASConstraintMaker* make) {
        make.left.mas_equalTo(16);
        make.right.mas_equalTo(-16);
        make.bottom.mas_equalTo(-SafeBottomHeight - 16);
        make.height.mas_equalTo(160);
    }];
}

- (UIImageView*)loadingView {
    if (_loadingView == nil) {
        _loadingView = [[UIImageView alloc] init];
        [_loadingView setImage:[UIImage imageNamed:@"loading"]];
        CABasicAnimation* anim = [CABasicAnimation animationWithKeyPath:@"transform.rotation"];
        anim.fromValue = @(0.0);
        anim.toValue = @(M_PI * 2.0);
        anim.duration = 1.0;
        anim.repeatCount = HUGE_VALF;
        [_loadingView.layer addAnimation:anim forKey:@"rotation"];
    }
    return _loadingView;
}

- (void)initData {
    SlarkPlayer* player = [[SlarkPlayer alloc] init:self.path];
    player.delegate = self;
    self.playerController = [[SlarkViewController alloc] initWithPlayer:self.view.bounds
                                                                 player:player];
    [self addChildViewController:self.playerController];
    [self.playerController.player prepare];
}

- (PlayerControllerView*)controllerView {
    if (_controllerView == nil) {
        _controllerView = [[PlayerControllerView alloc] initWithFrame:CGRectZero];
        @weakify(self);

        _controllerView.onBackClick = ^{
            @strongify(self);
            [self.hideControllerTimer invalidate];
            [self.playerController.player stop];
            [self.navigationController popViewControllerAnimated:YES];
            self.navigationController.navigationBar.hidden = NO;
        };

        _controllerView.onPlayClick = ^(BOOL isPlay) {
            @strongify(self);
            if (isPlay) {
                [self.playerController.player play];
            } else {
                [self.playerController.player pause];
            }
        };

        _controllerView.onSeekClick = ^(double time) {
            @strongify(self);
            [self.playerController.player seek:time];
        };

        _controllerView.onSeekDone = ^(double time) {
            @strongify(self);
            [self.playerController.player seek:time isAccurate:YES];
        };

        _controllerView.onSetLoopClick = ^(BOOL loop) {
            @strongify(self);
            self.playerController.player.isLoop = loop;
        };

        _controllerView.onSetMute = ^(BOOL isMute) {
            @strongify(self);
            self.playerController.player.isMute = isMute;
        };

        _controllerView.onSpeedChange = ^(float speed) {
            @strongify(self);
            // TODO: SlarkPlayer 暂未暴露 speed 属性，待 SDK 支持后在此设置
            (void)self;
        };

        _controllerView.onVolumeChange = ^(float volume) {
            @strongify(self);
            self.playerController.player.volume = volume;
        };

        _controllerView.onRotateClick = ^{
            @strongify(self);
            self.rotationAngle = (self.rotationAngle + 1) % 4;
            SlarkPlayerRotation rotations[] = {SlarkPlayerRotation_0,
                                               SlarkPlayerRotation_90,
                                               SlarkPlayerRotation_180,
                                               SlarkPlayerRotation_270};
            self.playerController.player.rotation = rotations[self.rotationAngle];
        };
        _controllerView.onRenderBackendChange = ^(BOOL useMetal) {
            @strongify(self);
            SlarkGraphicsBackend backend =
                useMetal ? SlarkGraphicsBackendMetal : SlarkGraphicsBackendOpenGLES;
            [self.playerController switchGraphicsBackend:backend];
        };
    }
    return _controllerView;
}

#pragma mark - ISlarkPlayerObserver

- (void)notifyTime:(NSString*)playerId time:(double)time {
    [self.controllerView updateCurrentTime:time];
}

- (void)notifyState:(NSString*)playerId state:(SlarkPlayerState)state {
    switch (state) {
        case SlarkPlayerStatePrepared:
            [self.controllerView
                updateTotalTime:CMTimeGetSeconds(self.playerController.player.totalDuration)];
            break;
        case SlarkPlayerStateCompleted:
        case SlarkPlayerStatePause:
            [self.controllerView setIsPause:YES];
            self.loadingView.hidden = YES;
            break;
        case SlarkPlayerStateReady:
        case SlarkPlayerStatePlaying:
            [self.controllerView setIsPause:NO];
            self.loadingView.hidden = YES;
            break;
        case SlarkPlayerStateBuffering:
            self.loadingView.hidden = NO;
            break;
        default:
            break;
    }
}

- (void)notifyEvent:(NSString*)playerId event:(SlarkPlayerEvent)event value:(NSString*)value {
    if (event == SlarkPlayerEventUpdateCacheTime) {
        [self.controllerView updateCacheTime:[value doubleValue]];
    } else if (event == SlarkPlayerEventOnError) {
        self.loadingView.hidden = YES;
        SlarkPlayerErrorCode code = (SlarkPlayerErrorCode)[value intValue];
        NSString* msg = @"error";
        if (code == SlarkPlayerErrorMediaOpenFailed || code == SlarkPlayerErrorPipelineOpenFailed) {
            msg = @"file error";
        } else if (code == SlarkPlayerErrorNetworkError ||
                   code == SlarkPlayerErrorConnectionFailed) {
            msg = @"network error";
        }
        [self showToast:msg duration:2.0];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.0 * NSEC_PER_SEC)),
                       dispatch_get_main_queue(),
                       ^{
                           [self.playerController.player stop];
                           [self.navigationController popViewControllerAnimated:YES];
                           self.navigationController.navigationBar.hidden = NO;
                       });
    }
}

- (void)showToast:(NSString*)message duration:(NSTimeInterval)duration {
    UIWindow* window = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes) {
        if ([scene isKindOfClass:[UIWindowScene class]] &&
            scene.activationState == UISceneActivationStateForegroundActive) {
            for (UIWindow* w in ((UIWindowScene*)scene).windows) {
                if (w.isKeyWindow) {
                    window = w;
                    break;
                }
            }
        }
    }
    if (!window) {
        return;
    }

    UILabel* label = [[UILabel alloc] init];
    label.text = message;
    label.textColor = [UIColor whiteColor];
    label.backgroundColor = [[UIColor blackColor] colorWithAlphaComponent:0.6];
    label.textAlignment = NSTextAlignmentCenter;
    label.font = [UIFont systemFontOfSize:14];
    label.layer.cornerRadius = 10;
    label.clipsToBounds = YES;

    CGFloat maxW = window.bounds.size.width - 40;
    CGRect rect = [message boundingRectWithSize:CGSizeMake(maxW, CGFLOAT_MAX)
                                        options:NSStringDrawingUsesLineFragmentOrigin
                                     attributes:@{NSFontAttributeName: label.font}
                                        context:nil];
    CGFloat w = MIN(rect.size.width + 32, maxW);
    CGFloat h = rect.size.height + 16;
    label.frame =
        CGRectMake((window.bounds.size.width - w) / 2, window.bounds.size.height - h - 100, w, h);
    label.alpha = 0.0;
    [window addSubview:label];
    [UIView animateWithDuration:0.3
        animations:^{
            label.alpha = 1.0;
        }
        completion:^(BOOL f) {
            [UIView animateWithDuration:0.5
                delay:duration
                options:UIViewAnimationOptionCurveEaseOut
                animations:^{
                    label.alpha = 0.0;
                }
                completion:^(BOOL f) {
                    [label removeFromSuperview];
                }];
        }];
}

@end
