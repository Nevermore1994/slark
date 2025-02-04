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
#include "Log.hpp"

using namespace slark;

@interface VideoViewController()<UIGestureRecognizerDelegate, ISlarkPlayerObserver>
@property (nonatomic, strong) PlayerControllerView* controllerView;
@property (nonatomic, strong) UILabel* nameLabel;
@property (nonatomic, assign) BOOL hasAuthorization;
@property (nonatomic, strong) SlarkViewController* playerController;
@property (nonatomic, strong) UIImageView* loadingView;
@end

@implementation VideoViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [self initData];
    [self initSubviews];
    LogI("video viewDidLoad");
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    self.navigationController.interactivePopGestureRecognizer.delegate = self;
    AVAuthorizationStatus AVstatus = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    switch (AVstatus) {
        case AVAuthorizationStatusAuthorized: {
            LogI("Authorized");
            self.hasAuthorization = YES;
        }
          break;
        case AVAuthorizationStatusDenied: {
            LogI("Denied");
            self.hasAuthorization = NO;
        }
          break;
        case AVAuthorizationStatusNotDetermined: {
            LogI("not Determined");
            @weakify(self);
            [self requestAccess:^(BOOL grant) {
                @strongify(self);
                self.hasAuthorization = grant;
            }];
        }
          break;
        case AVAuthorizationStatusRestricted: {
            LogI("Restricted");
            self.hasAuthorization = YES;
        }
          break;
      default:
          break;
    }
    [self setupAudioSession];
    LogI("video viewDidAppear");
}

- (void)requestAccess:(void(^)(BOOL)) block {
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:block];
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

- (void)initSubviews {
    self.navigationItem.title = @"video";
    [self.navigationController.navigationBar setTitleTextAttributes:@{NSForegroundColorAttributeName:[UIColor blackColor]}];
    self.view.backgroundColor = [UIColor blackColor];
    [self.view addSubview:self.controllerView];
    [self.view addSubview:self.loadingView];
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
    NSString *bundlePath = [[NSBundle mainBundle] pathForResource:@"resource" ofType:@"bundle"];
    NSBundle *resouceBundle = [NSBundle bundleWithPath:bundlePath];
    auto path = [resouceBundle pathForResource:@"Sample.mp4" ofType:@""];
    //auto path = @"http://devimages.apple.com/iphone/samples/bipbop/gear1/prog_index.m3u8";
    SlarkPlayer* player = [[SlarkPlayer alloc] init:path];
    player.delegate = self;
    self.playerController = [[SlarkViewController alloc] initWithPlayer:self.view.bounds player:player];
    [self addChildViewController:self.playerController];
    [self.view addSubview:self.playerController.view];
    self.hasAuthorization = NO;
}

- (void)handlePlayClick:(BOOL) isPlay {
    if (!isPlay) {
        [self.playerController.player pause];
        return;
    }
    if (!self.hasAuthorization) {
        @weakify(self);
        [self requestAccess:^(BOOL grant) {
            @strongify(self);
            self.hasAuthorization = grant;
            if (grant) {
                [self.playerController.player play];
            }
        }];
    } else {
        [self.playerController.player play];
    }
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
            LogI("seek to:{}", time);
        };
        _controllerView.onSeekDone = ^(double time){
            @strongify(self);
            [self.playerController.player seek:time isAccurate:YES];
            LogI("seek done");
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

- (void)notifyState:(NSString *)playerId state:(SlarkPlayerState)state {
    if (state == SlarkPlayerState::PlayerStateReady) {
        [self.controllerView updateTotalTime:CMTimeGetSeconds(self.playerController.player.totalDuration)];
        self.loadingView.hidden = YES;
    } else if (state == SlarkPlayerState::PlayerStateCompleted || state == SlarkPlayerState::PlayerStatePause) {
        [self.controllerView setIsPause:YES];
        self.loadingView.hidden = YES;
    } else if (state == SlarkPlayerState::PlayerStatePlaying) {
        [self.controllerView setIsPause:NO];
        self.loadingView.hidden = YES;
    } else if (state == SlarkPlayerState::PlayerStateBuffering) {
        self.loadingView.hidden = NO;
    }
}

- (void)notifyEvent:(NSString *)playerId event:(SlarkPlayerEvent)event value:(NSString *)value {
    if (event == SlarkPlayerEvent::PlayerEventUpdateCacheTime) {
        [self.controllerView updateCacheTime:[value doubleValue]];
    }
}
@end

