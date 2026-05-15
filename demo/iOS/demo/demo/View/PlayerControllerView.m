
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//

#import "PlayerControllerView.h"

#import "Masonry.h"

@interface FullImageButton : UIButton
@end

@implementation FullImageButton

- (CGRect)imageRectForContentRect:(CGRect)bounds {
    return CGRectMake(0.0, 0.0, self.frame.size.width, self.frame.size.height);
}

@end

@interface PlayerControllerView ()
@property (nonatomic, assign) BOOL isPause;
@property (nonatomic, assign) BOOL isLoop;
@property (nonatomic, assign) BOOL isMute;
@property (nonatomic, assign) BOOL useMetal;
@property (nonatomic, assign) BOOL isDragging;
@property (nonatomic, assign) float speed;
@property (nonatomic, strong) UIButton* backButton;
@property (nonatomic, strong) UIButton* rotateButton;
@property (nonatomic, strong) UIButton* renderBackendButton;
@property (nonatomic, strong) FullImageButton* playButton;
@property (nonatomic, strong) UIButton* prevButton;
@property (nonatomic, strong) UIButton* nextButton;
@property (nonatomic, strong) UIButton* loopButton;
@property (nonatomic, strong) UIButton* muteButton;
@property (nonatomic, strong) UIButton* speedButton;
@property (nonatomic, strong) UISlider* volumeSlider;
@property (nonatomic, strong) UISlider* progressSlider;
@property (nonatomic, strong) UIProgressView* cacheProgressView;
@property (nonatomic, strong) UILabel* currentTimeLabel;
@property (nonatomic, strong) UILabel* totalTimeLabel;
@property (nonatomic, assign) NSTimeInterval currentTime;
@property (nonatomic, assign) NSTimeInterval totalTime;
@end

static NSArray<NSNumber*>* speedOptions(void) {
    return @[@(0.5f), @(1.0f), @(1.5f), @(2.0f)];
}

@implementation PlayerControllerView

- (instancetype)initWithFrame:(CGRect)frame {
    if (self = [super initWithFrame:frame]) {
        _speed = 1.0f;
        _useMetal = NO;
        _isDragging = NO;
        [self setupSubviews];
    }
    return self;
}

- (void)setupSubviews {
    self.backgroundColor = [[UIColor blackColor] colorWithAlphaComponent:0.5];
    self.layer.cornerRadius = 14;
    self.clipsToBounds = YES;

    // 顶部栏
    [self addSubview:self.backButton];
    [self addSubview:self.rotateButton];
    [self addSubview:self.renderBackendButton];

    // 进度区
    [self addSubview:self.currentTimeLabel];
    [self addSubview:self.cacheProgressView];
    [self addSubview:self.progressSlider];
    [self addSubview:self.totalTimeLabel];

    // 底部控制栏
    [self addSubview:self.muteButton];
    [self addSubview:self.volumeSlider];
    [self addSubview:self.prevButton];
    [self addSubview:self.playButton];
    [self addSubview:self.nextButton];
    [self addSubview:self.loopButton];
    [self addSubview:self.speedButton];

    // 顶部栏布局
    [self.backButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.height.mas_equalTo(28);
        make.left.mas_equalTo(12);
        make.top.mas_equalTo(10);
    }];
    [self.rotateButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.height.mas_equalTo(28);
        make.centerX.equalTo(self);
        make.centerY.equalTo(self.backButton);
    }];
    [self.renderBackendButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.mas_equalTo(60);
        make.height.mas_equalTo(24);
        make.right.mas_equalTo(-12);
        make.centerY.equalTo(self.backButton);
    }];

    // 进度区布局
    [self.currentTimeLabel mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(20);
        make.width.mas_equalTo(52);
        make.left.mas_equalTo(12);
        make.top.mas_equalTo(self.backButton.mas_bottom).offset(10);
    }];
    [self.progressSlider mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(20);
        make.left.mas_equalTo(self.currentTimeLabel.mas_right).offset(6);
        make.right.mas_equalTo(self.totalTimeLabel.mas_left).offset(-6);
        make.centerY.equalTo(self.currentTimeLabel);
    }];
    [self.cacheProgressView mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(4);
        make.left.mas_equalTo(self.currentTimeLabel.mas_right).offset(6);
        make.right.mas_equalTo(self.totalTimeLabel.mas_left).offset(-6);
        make.centerY.equalTo(self.progressSlider);
    }];
    [self.totalTimeLabel mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(20);
        make.width.mas_equalTo(52);
        make.right.mas_equalTo(-12);
        make.centerY.equalTo(self.currentTimeLabel);
    }];

    // 底部控制栏布局
    [self.playButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.height.mas_equalTo(52);
        make.centerX.equalTo(self);
        make.top.mas_equalTo(self.progressSlider.mas_bottom).offset(10);
    }];
    [self.prevButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.height.mas_equalTo(40);
        make.right.mas_equalTo(self.playButton.mas_left).offset(-24);
        make.centerY.equalTo(self.playButton);
    }];
    [self.nextButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.height.mas_equalTo(40);
        make.left.mas_equalTo(self.playButton.mas_right).offset(24);
        make.centerY.equalTo(self.playButton);
    }];
    [self.muteButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.height.mas_equalTo(28);
        make.left.mas_equalTo(12);
        make.centerY.equalTo(self.playButton);
    }];
    [self.volumeSlider mas_makeConstraints:^(MASConstraintMaker* make) {
        make.left.mas_equalTo(self.muteButton.mas_right).offset(6);
        make.width.mas_equalTo(64);
        make.height.mas_equalTo(20);
        make.centerY.equalTo(self.playButton);
    }];
    [self.loopButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.height.mas_equalTo(28);
        make.right.mas_equalTo(self.speedButton.mas_left).offset(-12);
        make.centerY.equalTo(self.playButton);
    }];
    [self.speedButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.mas_equalTo(44);
        make.height.mas_equalTo(28);
        make.right.mas_equalTo(-12);
        make.centerY.equalTo(self.playButton);
    }];

    self.isPause = YES;
    self.isLoop = NO;
}

#pragma mark - Lazy Getters

- (UIButton*)backButton {
    if (_backButton == nil) {
        _backButton = [UIButton new];
        [_backButton setImage:[UIImage systemImageNamed:@"chevron.left"]
                     forState:UIControlStateNormal];
        _backButton.tintColor = [UIColor whiteColor];
        [_backButton addTarget:self
                        action:@selector(backClick)
              forControlEvents:UIControlEventTouchUpInside];
    }
    return _backButton;
}

- (UIButton*)rotateButton {
    if (_rotateButton == nil) {
        _rotateButton = [UIButton new];
        [_rotateButton setImage:[UIImage systemImageNamed:@"rotate.right"]
                       forState:UIControlStateNormal];
        _rotateButton.tintColor = [UIColor whiteColor];
        [_rotateButton addTarget:self
                          action:@selector(rotateClick)
                forControlEvents:UIControlEventTouchUpInside];
    }
    return _rotateButton;
}

- (UIButton*)renderBackendButton {
    if (_renderBackendButton == nil) {
        _renderBackendButton = [UIButton new];
        [_renderBackendButton setTitle:@"OpenGL ES" forState:UIControlStateNormal];
        _renderBackendButton.titleLabel.font = [UIFont systemFontOfSize:12
                                                                 weight:UIFontWeightMedium];
        [_renderBackendButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
        _renderBackendButton.layer.borderColor = [UIColor whiteColor].CGColor;
        _renderBackendButton.layer.borderWidth = 1.0;
        _renderBackendButton.layer.cornerRadius = 6;
        [_renderBackendButton addTarget:self
                                 action:@selector(renderBackendClick)
                       forControlEvents:UIControlEventTouchUpInside];
    }
    return _renderBackendButton;
}

- (FullImageButton*)playButton {
    if (_playButton == nil) {
        _playButton = [FullImageButton new];
        [_playButton setImage:[UIImage imageNamed:@"play_icon"] forState:UIControlStateNormal];
        _playButton.imageView.contentMode = UIViewContentModeScaleAspectFill;
        [_playButton addTarget:self
                        action:@selector(playClick)
              forControlEvents:UIControlEventTouchUpInside];
    }
    return _playButton;
}

- (UIButton*)prevButton {
    if (_prevButton == nil) {
        _prevButton = [UIButton new];
        [_prevButton setImage:[UIImage imageNamed:@"prev"] forState:UIControlStateNormal];
        _prevButton.imageView.contentMode = UIViewContentModeScaleAspectFit;
        [_prevButton addTarget:self
                        action:@selector(prevClick)
              forControlEvents:UIControlEventTouchUpInside];
    }
    return _prevButton;
}

- (UIButton*)nextButton {
    if (_nextButton == nil) {
        _nextButton = [UIButton new];
        [_nextButton setImage:[UIImage imageNamed:@"next"] forState:UIControlStateNormal];
        _nextButton.imageView.contentMode = UIViewContentModeScaleAspectFit;
        [_nextButton addTarget:self
                        action:@selector(nextClick)
              forControlEvents:UIControlEventTouchUpInside];
    }
    return _nextButton;
}

- (UIButton*)loopButton {
    if (_loopButton == nil) {
        _loopButton = [UIButton new];
        [_loopButton setImage:[UIImage imageNamed:@"loop"] forState:UIControlStateNormal];
        _loopButton.imageView.contentMode = UIViewContentModeScaleAspectFit;
        [_loopButton addTarget:self
                        action:@selector(loopClick)
              forControlEvents:UIControlEventTouchUpInside];
    }
    return _loopButton;
}

- (UIButton*)muteButton {
    if (_muteButton == nil) {
        _muteButton = [UIButton new];
        [_muteButton setImage:[UIImage imageNamed:@"volume"] forState:UIControlStateNormal];
        _muteButton.imageView.contentMode = UIViewContentModeScaleAspectFit;
        [_muteButton addTarget:self
                        action:@selector(muteClick)
              forControlEvents:UIControlEventTouchUpInside];
    }
    return _muteButton;
}

- (UISlider*)volumeSlider {
    if (_volumeSlider == nil) {
        _volumeSlider = [UISlider new];
        _volumeSlider.minimumValue = 0.f;
        _volumeSlider.maximumValue = 100.f;
        _volumeSlider.value = 100.f;
        _volumeSlider.minimumTrackTintColor = [UIColor whiteColor];
        _volumeSlider.maximumTrackTintColor = [[UIColor whiteColor] colorWithAlphaComponent:0.3];
        UIImage* thumb = [self thumbImageWithRadius:5.0 color:[UIColor whiteColor]];
        [_volumeSlider setThumbImage:thumb forState:UIControlStateNormal];
        [_volumeSlider setThumbImage:thumb forState:UIControlStateHighlighted];
        [_volumeSlider addTarget:self
                          action:@selector(volumeChanged)
                forControlEvents:UIControlEventValueChanged];
    }
    return _volumeSlider;
}

- (UIButton*)speedButton {
    if (_speedButton == nil) {
        _speedButton = [UIButton new];
        [_speedButton setTitle:@"1.0x" forState:UIControlStateNormal];
        _speedButton.titleLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightMedium];
        [_speedButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
        _speedButton.layer.borderColor = [UIColor whiteColor].CGColor;
        _speedButton.layer.borderWidth = 1.0;
        _speedButton.layer.cornerRadius = 6;
        [_speedButton addTarget:self
                         action:@selector(speedClick)
               forControlEvents:UIControlEventTouchUpInside];
    }
    return _speedButton;
}

- (UIProgressView*)cacheProgressView {
    if (_cacheProgressView == nil) {
        _cacheProgressView = [UIProgressView new];
        _cacheProgressView.trackTintColor = [[UIColor whiteColor] colorWithAlphaComponent:0.3];
        _cacheProgressView.progressTintColor = [[UIColor alloc] initWithRed:172.0f / 255.0f
                                                                      green:32.0f / 255.0f
                                                                       blue:219.0f / 255.0f
                                                                      alpha:1.f];
        _cacheProgressView.layer.cornerRadius = 2;
        _cacheProgressView.layer.masksToBounds = YES;
    }
    return _cacheProgressView;
}

- (UISlider*)progressSlider {
    if (_progressSlider == nil) {
        _progressSlider = [UISlider new];
        _progressSlider.maximumValue = 100.f;
        _progressSlider.minimumValue = 0.f;
        _progressSlider.minimumTrackTintColor = [[UIColor alloc] initWithRed:26.0f / 255.0f
                                                                       green:109.0f / 255.0f
                                                                        blue:1.f
                                                                       alpha:1.f];
        _progressSlider.maximumTrackTintColor = [UIColor clearColor];
        [_progressSlider addTarget:self
                            action:@selector(progressChanged)
                  forControlEvents:UIControlEventValueChanged];
        [_progressSlider addTarget:self
                            action:@selector(seekDone)
                  forControlEvents:UIControlEventTouchUpInside];
        _progressSlider.layer.cornerRadius = 10;
        _progressSlider.layer.masksToBounds = YES;
        UIImage* thumb = [self thumbImageWithRadius:6.0
                                              color:[[UIColor alloc] initWithRed:200.0f / 255.0f
                                                                           green:50.0f / 255.0f
                                                                            blue:219.0f / 255.0f
                                                                           alpha:1.f]];
        [_progressSlider setThumbImage:thumb forState:UIControlStateNormal];
        [_progressSlider setThumbImage:thumb forState:UIControlStateHighlighted];
    }
    return _progressSlider;
}

- (UIImage*)thumbImageWithRadius:(CGFloat)radius color:(UIColor*)color {
    CGFloat size = radius * 2;
    UIGraphicsBeginImageContextWithOptions(CGSizeMake(size, size), NO, 0);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGRect bound = CGRectMake(0, 0, size, size);
    UIBezierPath* path = [UIBezierPath bezierPathWithRoundedRect:bound cornerRadius:radius];
    [path addClip];
    CGContextSetFillColorWithColor(ctx, color.CGColor);
    CGContextFillRect(ctx, bound);
    UIImage* image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return image;
}

- (UILabel*)currentTimeLabel {
    if (_currentTimeLabel == nil) {
        _currentTimeLabel = [[UILabel alloc] initWithFrame:CGRectZero];
        _currentTimeLabel.text = [self timeString:0];
        _currentTimeLabel.textAlignment = NSTextAlignmentCenter;
        _currentTimeLabel.font = [UIFont monospacedDigitSystemFontOfSize:12
                                                                  weight:UIFontWeightRegular];
        _currentTimeLabel.textColor = [UIColor whiteColor];
    }
    return _currentTimeLabel;
}

- (UILabel*)totalTimeLabel {
    if (_totalTimeLabel == nil) {
        _totalTimeLabel = [[UILabel alloc] initWithFrame:CGRectZero];
        _totalTimeLabel.text = [self timeString:0];
        _totalTimeLabel.textAlignment = NSTextAlignmentCenter;
        _totalTimeLabel.font = [UIFont monospacedDigitSystemFontOfSize:12
                                                                weight:UIFontWeightRegular];
        _totalTimeLabel.textColor = [UIColor whiteColor];
    }
    return _totalTimeLabel;
}

#pragma mark - Actions

- (void)backClick {
    !self.onBackClick ?: self.onBackClick();
}

- (void)rotateClick {
    !self.onRotateClick ?: self.onRotateClick();
}

- (void)renderBackendClick {
    _useMetal = !_useMetal;
    NSString* title = _useMetal ? @"Metal" : @"OpenGL ES";
    [self.renderBackendButton setTitle:title forState:UIControlStateNormal];
    !self.onRenderBackendChange ?: self.onRenderBackendChange(_useMetal);
}

- (void)playClick {
    self.isPause = !self.isPause;
}

- (void)prevClick {
    !self.onPrevClick ?: self.onPrevClick();
}

- (void)nextClick {
    !self.onNextClick ?: self.onNextClick();
}

- (void)loopClick {
    self.isLoop = !self.isLoop;
    !self.onSetLoopClick ?: self.onSetLoopClick(self.isLoop);
}

- (void)muteClick {
    self.isMute = !self.isMute;
    !self.onSetMute ?: self.onSetMute(self.isMute);
    UIImage* img = [UIImage imageNamed:self.isMute ? @"mute" : @"volume"];
    [self.muteButton setImage:img forState:UIControlStateNormal];
    self.volumeSlider.alpha = self.isMute ? 0.4f : 1.0f;
}

- (void)volumeChanged {
    !self.onVolumeChange ?: self.onVolumeChange(self.volumeSlider.value);
}

- (void)progressChanged {
    _isDragging = YES;
    self.currentTimeLabel.text = [self timeString:self.progressSlider.value];
    !self.onSeekClick ?: self.onSeekClick(self.progressSlider.value);
}

- (void)seekDone {
    _isDragging = NO;
    !self.onSeekDone ?: self.onSeekDone(self.progressSlider.value);
}

- (void)speedClick {
    NSArray<NSNumber*>* options = speedOptions();
    NSUInteger idx =
        [options indexOfObjectPassingTest:^BOOL(NSNumber* n, NSUInteger i, BOOL* stop) {
            return fabsf(n.floatValue - self.speed) < 0.01f;
        }];
    NSUInteger nextIdx = (idx == NSNotFound) ? 1 : (idx + 1) % options.count;
    self.speed = options[nextIdx].floatValue;
    NSString* title = (self.speed == (int)self.speed)
                          ? [NSString stringWithFormat:@"%.0fx", self.speed]
                          : [NSString stringWithFormat:@"%.1fx", self.speed];
    [self.speedButton setTitle:title forState:UIControlStateNormal];
    !self.onSpeedChange ?: self.onSpeedChange(self.speed);
}

#pragma mark - Setters

- (void)setIsLoop:(BOOL)isLoop {
    _isLoop = isLoop;
    UIImage* img = [UIImage imageNamed:isLoop ? @"loop" : @"stop_loop"];
    [self.loopButton setImage:img forState:UIControlStateNormal];
}

- (void)setIsPause:(BOOL)isPause {
    _isPause = isPause;
    UIImage* img = [UIImage imageNamed:isPause ? @"play_icon" : @"pause_icon"];
    [self.playButton setImage:img forState:UIControlStateNormal];
    !self.onPlayClick ?: self.onPlayClick(!isPause);
}

#pragma mark - Public Methods

- (void)updateCurrentTime:(NSTimeInterval)value {
    self.currentTime = value;
}

- (void)updateTotalTime:(NSTimeInterval)value {
    self.totalTime = value;
}

- (void)updateVolume:(float)volume {
    self.volumeSlider.value = volume;
}

- (void)setTotalTime:(NSTimeInterval)totalTime {
    _totalTime = totalTime;
    self.progressSlider.maximumValue = _totalTime;
    self.totalTimeLabel.text = [self timeString:_totalTime];
}

- (void)setCurrentTime:(NSTimeInterval)currentTime {
    _currentTime = currentTime;
    if (!_isDragging) {
        self.progressSlider.value = currentTime;
        self.currentTimeLabel.text = [self timeString:_currentTime];
    }
}

- (void)updateCacheTime:(NSTimeInterval)value {
    if (_totalTime > 0.0) {
        double progress = value / _totalTime;
        if (fabs(progress - 1.0) < 0.01) {
            progress = 1.0;
        }
        self.cacheProgressView.progress = progress;
    }
}

- (NSString*)timeString:(NSTimeInterval)time {
    if (time < 0) {
        time = 0;
    } else if (time > self.totalTime) {
        time = self.totalTime;
    }
    int hour = (int)(time / 3600.0);
    time -= hour * 3600.0;
    int mi = (int)(time / 60.0);
    int seconds = (int)round(time - mi * 60.0);
    if (hour > 0) {
        return [NSString stringWithFormat:@"%02d:%02d:%02d", hour, mi, seconds];
    }
    return [NSString stringWithFormat:@"%02d:%02d", mi, seconds];
}

@end
