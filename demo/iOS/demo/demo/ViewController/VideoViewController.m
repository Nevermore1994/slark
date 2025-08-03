//
//  VideoViewController.m
//  demo
//
//  Created by Nevermore on 2024/10/12.
//

#import <AVFoundation/AVFoundation.h>
#import "Masonry.h"
#import "EXTScope.h"
#import "VideoViewController.h"
#import "PlayerControllerView.h"
#import "iOSUtil.h"
#import "SlarkViewController.h"

@interface VideoViewController()<UIGestureRecognizerDelegate, ISlarkPlayerObserver>
@property (nonatomic, strong) PlayerControllerView* controllerView;
@property (nonatomic, strong) UILabel* nameLabel;
@property (nonatomic, assign) BOOL hasAuthorization;
@property (nonatomic, strong) SlarkViewController* playerController;
@property (nonatomic, strong) UIImageView* loadingView;
@property (nonatomic, strong) NSTimer* hideControllerTimer;
@end

@implementation VideoViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [self initData];
    [self initSubviews];
    UITapGestureRecognizer *tap = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(handleTap:)];
    [self.view addGestureRecognizer:tap];
    [self resetHideControllerTimer];
    NSLog(@"video viewDidLoad");
}

- (void)handleTap:(UITapGestureRecognizer *)gesture {
    self.controllerView.hidden = !self.controllerView.hidden;
    if (!self.controllerView.hidden) {
        [self resetHideControllerTimer];
    }
}

- (void)resetHideControllerTimer {
    [self.hideControllerTimer invalidate];
    self.hideControllerTimer = [NSTimer scheduledTimerWithTimeInterval:5.0 target:self selector:@selector(dismissControllView) userInfo:nil repeats:NO];
    self.navigationController.navigationBar.hidden = NO;
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    self.navigationController.interactivePopGestureRecognizer.delegate = self;
    [self setupAudioSession];
    NSLog(@"video viewDidAppear");
}

- (void)setupAudioSession {
    AVAudioSession *session = [AVAudioSession sharedInstance];
    NSError *error = nil;
    [session setCategory:AVAudioSessionCategoryPlayback error:&error];
    [session setActive:YES error:&error];
}

- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer *)gestureRecognizer {
    return NO;
}

- (void)dealloc {
    [self.hideControllerTimer invalidate];
    [self.playerController removeFromParentViewController];
    [self.navigationController popViewControllerAnimated:YES];
}

- (void)initSubviews {
    self.navigationItem.title = @"video";
    [self.navigationController.navigationBar setTitleTextAttributes:@{NSForegroundColorAttributeName:[UIColor blackColor]}];
    self.view.backgroundColor = [UIColor blackColor];
    [self.view addSubview:self.playerController.view];
    [self.view addSubview:self.controllerView];
    [self.view addSubview:self.loadingView];
    [self.playerController.view mas_makeConstraints:^(MASConstraintMaker *make) {
        make.width.mas_equalTo(self.view);
        make.height.mas_equalTo(self.view);
        make.centerX.equalTo(self.view);
        make.centerY.equalTo(self.view);
    }];
    [self.controllerView mas_makeConstraints:^(MASConstraintMaker *make) {
        make.width.mas_equalTo(self.view.mas_width);
        make.height.mas_equalTo(120);
        make.centerX.equalTo(self.view);
        make.bottom.mas_equalTo(-SafeBottomHeight).mas_offset(-30);
    }];
    [self.loadingView mas_makeConstraints:^(MASConstraintMaker *make) {
        make.width.mas_equalTo(54);
        make.height.mas_equalTo(54);
        make.centerX.equalTo(self.view);
        make.centerY.equalTo(self.view);
    }];
}

- (UIImageView*)loadingView {
    if (_loadingView == nil) {
        const CGFloat size = 100;
        _loadingView = [[UIImageView alloc] initWithFrame:CGRectMake((CGRectGetWidth(self.view.bounds) - size) / 2, (CGRectGetHeight(self.view.bounds) - size) / 2, size, size)];
        [_loadingView setImage:[UIImage imageNamed:@"loading"]];
        CABasicAnimation *rotateAnimation = [CABasicAnimation animationWithKeyPath:@"transform.rotation"];
        rotateAnimation.fromValue = @(0.0);
        rotateAnimation.toValue = @(M_PI * 2.0);
        rotateAnimation.duration = 1.0;
        rotateAnimation.repeatCount = HUGE_VALF;
        [_loadingView.layer addAnimation:rotateAnimation forKey:@"rotationAnimation"];
    }
    return _loadingView;
}

- (void)initData {
    SlarkPlayer* player = [[SlarkPlayer alloc] init:self.path];
    player.delegate = self;
    self.playerController = [[SlarkViewController alloc] initWithPlayer:self.view.bounds player:player];
    [self addChildViewController:self.playerController];
    self.hasAuthorization = NO;
    [self.playerController.player prepare];
}

- (void)handlePlayClick:(BOOL) isPlay {
    if (!isPlay) {
        [self.playerController.player pause];
        return;
    }
    [self.playerController.player play];
}

- (PlayerControllerView*)controllerView{
    if (_controllerView == nil) {
        _controllerView = [[PlayerControllerView alloc] initWithFrame:CGRectZero];
        @weakify(self);
        _controllerView.onPlayClick = ^(BOOL isPlay){
            @strongify(self);
            [self handlePlayClick:isPlay];
        };
        _controllerView.onSeekClick = ^(double time) {
            @strongify(self);
            [self.playerController.player seek:time];
            NSLog(@"seek to: %.2f", time);
        };
        _controllerView.onSeekDone = ^(double time){
            @strongify(self);
            [self.playerController.player seek:time isAccurate:YES];
            NSLog(@"seek done");
        };
        _controllerView.onSetLoopClick = ^(BOOL loop) {
            @strongify(self);
            [self.playerController.player setLoop:loop];
        };
        _controllerView.onSetMute = ^(BOOL isMute) {
            @strongify(self);
            [self.playerController.player setMute:isMute];
        };
    }
    return _controllerView;
}

- (void)notifyTime:(NSString *)playerId time:(double)time {
    [self.controllerView updateCurrentTime:time];
}

- (void)dismissControllView {
    self.controllerView.hidden = YES;
    self.navigationController.navigationBar.hidden = YES;
}

- (void)notifyState:(NSString *)playerId state:(SlarkPlayerState)state {
    if (state == SlarkPlayerStatePrepared) {
        [self.controllerView updateTotalTime:CMTimeGetSeconds(self.playerController.player.totalDuration)];
    } else if (state == SlarkPlayerStateCompleted ||
               state == SlarkPlayerStatePause) {
        [self.controllerView setIsPause:YES];
        self.loadingView.hidden = YES;
    } else if (state == SlarkPlayerStateReady) {
        self.loadingView.hidden = YES;
    } else if (state == SlarkPlayerStatePlaying) {
        [self.controllerView setIsPause:NO];
        self.loadingView.hidden = YES;
    } else if (state == SlarkPlayerStateBuffering) {
        self.loadingView.hidden = NO;
    }
}

- (void)notifyEvent:(NSString *)playerId event:(SlarkPlayerEvent)event value:(NSString *)value {
    if (event == SlarkPlayerEventUpdateCacheTime) {
        [self.controllerView updateCacheTime:[value doubleValue]];
    } else if (event == SlarkPlayerEventOnError) {
        self.loadingView.hidden = YES;
        SlarkPlayerErrorCode errorCode = (SlarkPlayerErrorCode)[value intValue];
        if (errorCode == SlarkPlayerErrorFileError) {
            [self ShowToast:@"file error" duration:2.0];
        } else if (errorCode == SlarkPlayerErrorNetWorkError) {
            [self ShowToast:@"network error" duration:2.0];
        } else {
            [self ShowToast:@"error" duration:2.0];
        }
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            [self.playerController.player stop];
            [self.navigationController popViewControllerAnimated:YES];
        });
    }
}

- (UIWindow *)currentWindow {
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if ([scene isKindOfClass:[UIWindowScene class]] &&
            scene.activationState == UISceneActivationStateForegroundActive) {
            UIWindowScene *windowScene = (UIWindowScene *)scene;
            for (UIWindow *window in windowScene.windows) {
                if (window.isKeyWindow) {
                    return window;
                }
            }
        }
    }
    return nil;
}

- (void)ShowToast:(NSString*) message duration:(NSTimeInterval) duration {
    UIWindow *window = [self currentWindow];
    if (!window) return;

    UILabel *toastLabel = [[UILabel alloc] init];
    toastLabel.text = message;
    toastLabel.textColor = [UIColor whiteColor];
    toastLabel.backgroundColor = [[UIColor blackColor] colorWithAlphaComponent:0.6];
    toastLabel.textAlignment = NSTextAlignmentCenter;
    toastLabel.font = [UIFont systemFontOfSize:14];
    toastLabel.numberOfLines = 0;
    toastLabel.layer.cornerRadius = 10;
    toastLabel.clipsToBounds = YES;

    CGFloat maxWidth = window.bounds.size.width - 40;
    CGSize maxSize = CGSizeMake(maxWidth, CGFLOAT_MAX);
    CGRect expectedRect = [toastLabel.text boundingRectWithSize:maxSize
                                                         options:NSStringDrawingUsesLineFragmentOrigin
                                                      attributes:@{NSFontAttributeName: toastLabel.font}
                                                         context:nil];
    
    CGFloat padding = 16;
    CGFloat labelWidth = MIN(expectedRect.size.width + padding, maxWidth);
    CGFloat labelHeight = expectedRect.size.height + padding;

    toastLabel.frame = CGRectMake((window.bounds.size.width - labelWidth) / 2,
                                  window.bounds.size.height - labelHeight - 100,
                                  labelWidth,
                                  labelHeight);
    
    toastLabel.alpha = 0.0;
    [window addSubview:toastLabel];

    [UIView animateWithDuration:0.3 animations:^{
        toastLabel.alpha = 1.0;
    } completion:^(BOOL) {
        [UIView animateWithDuration:0.5
                              delay:duration
                            options:UIViewAnimationOptionCurveEaseOut
                         animations:^{
            toastLabel.alpha = 0.0;
        } completion:^(BOOL) {
            [toastLabel removeFromSuperview];
        }];
    }];
}
@end

