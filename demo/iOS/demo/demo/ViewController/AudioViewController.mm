//
//  AudioViewController.m
//  demo
//


#import <AVFoundation/AVFoundation.h>
#import "Masonry.h"
#import "EXTScope.h"
#import "AudioViewController.h"
#import "PlayerControllerView.h"
#include "SlarkPlayer.h"
#include "Log.hpp"

using namespace slark;

@interface AudioViewController()<UIGestureRecognizerDelegate, ISlarkPlayerObserver>
@property (nonatomic, strong) UIImageView* iconView;
@property (nonatomic, strong) PlayerControllerView* controllerView;
@property (nonatomic, strong) UILabel* nameLabel;
@property (nonatomic, assign) BOOL hasAuthorization;
@property (nonatomic, strong) SlarkPlayer* player;
@end


@implementation AudioViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    [self initSubviews];
    [self initData];
    LogI("audio viewDidLoad");
}

- (void)dealloc {
    self.player = nil;
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
    LogI("audio viewDidAppear");
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
    self.navigationItem.title = @"music";
    [self.navigationController.navigationBar setTitleTextAttributes:@{NSForegroundColorAttributeName:[UIColor blackColor]}];
    self.view.backgroundColor = [UIColor blackColor];
    [self.view addSubview:self.iconView];
    [self.view addSubview:self.controllerView];
    [self.iconView mas_makeConstraints:^(MASConstraintMaker *make) {
        UIEdgeInsets insets = UIEdgeInsetsMake(100, 60, 300, 60);
        make.edges.equalTo(self.view).with.insets(insets);
    }];
    [self.controllerView mas_makeConstraints:^(MASConstraintMaker *make) {
        make.width.mas_equalTo(self.view.mas_width);
        make.height.mas_equalTo(120);
        make.centerX.equalTo(self.view);
        make.top.mas_equalTo(self.iconView.mas_bottom).mas_offset(40);
    }];
}

- (void)initData {
    NSString *bundlePath = [[NSBundle mainBundle] pathForResource:@"resource" ofType:@"bundle"];
    NSBundle *resouceBundle = [NSBundle bundleWithPath:bundlePath];
    auto path = [resouceBundle pathForResource:@"sample-3s.wav" ofType:@""];
    self.player = [[SlarkPlayer alloc] init:path];
    self.player.delegate = self;
    self.hasAuthorization = NO;
}

- (UIView*)iconView {
    if (_iconView == nil) {
        _iconView = [[UIImageView alloc] init];
        _iconView.image = [UIImage imageNamed:@"music_normal"];
        _iconView.contentMode = UIViewContentModeScaleAspectFit;
    }
    return _iconView;
}

- (void)handlePlayClick:(BOOL) isPlay {
    if (!isPlay) {
        [self.player pause];
        return;
    }
    if (!self.hasAuthorization) {
        @weakify(self);
        [self requestAccess:^(BOOL grant) {
            @strongify(self);
            self.hasAuthorization = grant;
            if (grant) {
                [self.player play];
            }
        }];
    } else {
        [self.player play];
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
            [self.player seek:time];
            LogI("audio seek:{}", time);
        };
        _controllerView.onSeekDone = ^(double time){
            @strongify(self);
            [self.player seek:time isAccurate:YES];
            LogI("seek done");
        };
        _controllerView.onSetLoopClick = ^(BOOL loop) {
            @strongify(self);
            [self.player setLoop:loop];
        };
    }
    return _controllerView;
}

- (void)notifyTime:(NSString *)playerId time:(double)time {
    [self.controllerView updateCurrentTime:time];
}

- (void)notifyState:(NSString *)playerId state:(SlarkPlayerState)state {
    if (state == SlarkPlayerState::PlayerStateReady) {
        [self.controllerView updateTotalTime:CMTimeGetSeconds(self.player.totalDuration)];
    } else if (state == SlarkPlayerState::PlayerStateCompleted || state == SlarkPlayerState::PlayerStatePause) {
        [self.controllerView setIsPause:YES];
    } else if (state == SlarkPlayerState::PlayerStatePlaying) {
        [self.controllerView setIsPause:NO];
    }
}

- (void)notifyEvent:(NSString *)playerId event:(SlarkPlayerEvent)event value:(NSString *)value {
    if (event == SlarkPlayerEvent::PlayerEventUpdateCacheTime) {
        [self.controllerView updateCacheTime:[value doubleValue]];
    }
}
@end
