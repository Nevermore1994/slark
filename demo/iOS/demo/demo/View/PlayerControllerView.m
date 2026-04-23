
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//

#import "PlayerControllerView.h"

#import "Masonry.h"

@interface Button : UIButton
@end

@implementation Button

- (CGRect)imageRectForContentRect:(CGRect)bounds {
    return CGRectMake(0.0, 0.0, self.frame.size.width, self.frame.size.height);
}

@end

@interface PlayerControllerView ()
@property (nonatomic, assign) BOOL isPause;
@property (nonatomic, assign) BOOL isLoop;
@property (nonatomic, assign) BOOL isMute;
@property (nonatomic, assign) float speed;
@property (nonatomic, strong) Button* playButton;
@property (nonatomic, strong) UIButton* prevButton;
@property (nonatomic, strong) UIButton* nextButton;
@property (nonatomic, strong) UIButton* loopButton;
@property (nonatomic, strong) UIButton* volumeButton;
@property (nonatomic, strong) UIButton* speedButton;
@property (nonatomic, strong) UIButton* backButton;
@property (nonatomic, strong) UISlider* progressView;
@property (nonatomic, strong) UIProgressView* cacheProgressView;
@property (nonatomic, strong) UILabel* currentTimeLabel;
@property (nonatomic, strong) UILabel* totalTimeLabel;
@property (nonatomic, assign) NSTimeInterval currentTime;
@property (nonatomic, assign) NSTimeInterval totalTime;
@end

static NSArray<NSNumber*>* speedOptions() {
    return @[@(0.5f), @(1.0f), @(1.5f), @(2.0f)];
}

@implementation PlayerControllerView

- (instancetype)initWithFrame:(CGRect)frame {
    if (self = [super initWithFrame:frame]) {
        _speed = 1.0f;
        [self initSubviews];
    }
    return self;
}

- (void)initSubviews {
    self.backgroundColor = [[UIColor blackColor] colorWithAlphaComponent:0.45];
    self.layer.cornerRadius = 12;
    self.clipsToBounds = YES;

    [self addSubview:self.backButton];
    [self addSubview:self.currentTimeLabel];
    [self addSubview:self.cacheProgressView];
    [self addSubview:self.progressView];
    [self addSubview:self.totalTimeLabel];
    [self addSubview:self.prevButton];
    [self addSubview:self.playButton];
    [self addSubview:self.nextButton];
    [self addSubview:self.volumeButton];
    [self addSubview:self.loopButton];
    [self addSubview:self.speedButton];

    [self.backButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.height.mas_equalTo(28);
        make.left.mas_equalTo(12);
        make.top.mas_equalTo(10);
    }];

    [self.currentTimeLabel mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(20);
        make.width.mas_equalTo(52);
        make.left.mas_equalTo(12);
        make.top.mas_equalTo(self.backButton.mas_bottom).offset(8);
    }];
    [self.progressView mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(20);
        make.left.mas_equalTo(self.currentTimeLabel.mas_right).offset(6);
        make.right.mas_equalTo(self.totalTimeLabel.mas_left).offset(-6);
        make.centerY.equalTo(self.currentTimeLabel);
    }];
    [self.cacheProgressView mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(4);
        make.left.mas_equalTo(self.currentTimeLabel.mas_right).offset(6);
        make.right.mas_equalTo(self.totalTimeLabel.mas_left).offset(-6);
        make.centerY.equalTo(self.progressView);
    }];
    [self.totalTimeLabel mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(20);
        make.width.mas_equalTo(52);
        make.right.mas_equalTo(-12);
        make.centerY.equalTo(self.currentTimeLabel);
    }];

    [self.playButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.height.mas_equalTo(52);
        make.centerX.equalTo(self);
        make.top.mas_equalTo(self.progressView.mas_bottom).offset(10);
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
    [self.volumeButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.width.height.mas_equalTo(28);
        make.left.mas_equalTo(12);
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

- (UIButton*)playButton {
    if (_playButton == nil) {
        _playButton = [Button new];
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

- (UIButton*)volumeButton {
    if (_volumeButton == nil) {
        _volumeButton = [UIButton new];
        [_volumeButton setImage:[UIImage imageNamed:@"volume"] forState:UIControlStateNormal];
        _volumeButton.imageView.contentMode = UIViewContentModeScaleAspectFit;
        [_volumeButton addTarget:self
                          action:@selector(volumeClick)
                forControlEvents:UIControlEventTouchUpInside];
    }
    return _volumeButton;
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

- (UISlider*)progressView {
    if (_progressView == nil) {
        _progressView = [UISlider new];
        _progressView.maximumValue = 100.f;
        _progressView.minimumValue = 0.f;
        _progressView.minimumTrackTintColor = [[UIColor alloc] initWithRed:26.0f / 255.0f
                                                                     green:109.0f / 255.0f
                                                                      blue:1.f
                                                                     alpha:1.f];
        _progressView.maximumTrackTintColor = [UIColor clearColor];
        [_progressView addTarget:self
                          action:@selector(progressChanged)
                forControlEvents:UIControlEventValueChanged];
        [_progressView addTarget:self
                          action:@selector(seekDone)
                forControlEvents:UIControlEventTouchUpInside];
        _progressView.layer.cornerRadius = 10;
        _progressView.layer.masksToBounds = YES;
        UIImage* thumb = [self thumbImageWithColor:[[UIColor alloc] initWithRed:200.0f / 255.0f
                                                                          green:50.0f / 255.0f
                                                                           blue:219.0f / 255.0f
                                                                          alpha:1.f]];
        [_progressView setThumbImage:thumb forState:UIControlStateNormal];
        [_progressView setThumbImage:thumb forState:UIControlStateHighlighted];
    }
    return _progressView;
}

- (UIImage*)thumbImageWithColor:(UIColor*)color {
    CGSize size = CGSizeMake(12.0, 12.0);
    UIGraphicsBeginImageContextWithOptions(size, NO, 0);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGRect bound = CGRectMake(0, 0, size.width, size.height);
    UIBezierPath* path = [UIBezierPath bezierPathWithRoundedRect:bound cornerRadius:60.0];
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

- (void)progressChanged {
    self.currentTimeLabel.text = [self timeString:self.progressView.value];
    !self.onSeekClick ?: self.onSeekClick(self.progressView.value);
}

- (void)seekDone {
    !self.onSeekDone ?: self.onSeekDone(self.progressView.value);
}

- (void)backClick {
    !self.onBackClick ?: self.onBackClick();
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

- (void)volumeClick {
    self.isMute = !self.isMute;
    !self.onSetMute ?: self.onSetMute(self.isMute);
    UIImage* img = [UIImage imageNamed:self.isMute ? @"mute" : @"volume"];
    [self.volumeButton setImage:img forState:UIControlStateNormal];
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

- (void)updateCurrentTime:(NSTimeInterval)value {
    self.currentTime = value;
}

- (void)updateTotalTime:(NSTimeInterval)value {
    self.totalTime = value;
}

- (void)setTotalTime:(NSTimeInterval)totalTime {
    _totalTime = totalTime;
    self.progressView.maximumValue = _totalTime;
    self.totalTimeLabel.text = [self timeString:_totalTime];
}

- (void)setCurrentTime:(NSTimeInterval)currentTime {
    _currentTime = currentTime;
    self.progressView.value = currentTime;
    self.currentTimeLabel.text = [self timeString:_currentTime];
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

@interface PlayerControllerView ()
@property (nonatomic, assign) BOOL isPause;
@property (nonatomic, assign) BOOL isLoop;
@property (nonatomic, assign) BOOL isMute;
@property (nonatomic, strong) Button* playButton;
@property (nonatomic, strong) UIButton* prevButton;
@property (nonatomic, strong) UIButton* nextButton;
@property (nonatomic, strong) UIButton* loopButton;
@property (nonatomic, strong) UIButton* volumeButton;
@property (nonatomic, strong) UISlider* progressView;
@property (nonatomic, strong) UIProgressView* cacheProgressView;
@property (nonatomic, strong) UILabel* currentTimeLabel;
@property (nonatomic, strong) UILabel* totalTimeLabel;
@property (nonatomic, assign) NSTimeInterval currentTime;
@property (nonatomic, assign) NSTimeInterval totalTime;
@end

@implementation PlayerControllerView

- (instancetype)initWithFrame:(CGRect)frame {
    if (self = [super initWithFrame:frame]) {
        [self initSubviews];
    }
    return self;
}

- (void)initSubviews {
    [self addSubview:self.playButton];
    [self addSubview:self.prevButton];
    [self addSubview:self.nextButton];
    [self addSubview:self.loopButton];
    [self addSubview:self.volumeButton];
    [self addSubview:self.cacheProgressView];
    [self addSubview:self.progressView];
    [self addSubview:self.currentTimeLabel];
    [self addSubview:self.totalTimeLabel];
    [self.currentTimeLabel mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(20);
        make.width.mas_equalTo(40);
        make.left.mas_equalTo(self.mas_left).offset(10);
        make.top.mas_equalTo(self.mas_top).mas_offset(20);
    }];
    [self.progressView mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(20);
        make.left.mas_equalTo(self.currentTimeLabel.mas_right).offset(5);
        make.width.mas_equalTo(self.mas_width).offset(-110);
        make.top.mas_equalTo(self.mas_top).mas_offset(20);
    }];
    self.progressView.layer.cornerRadius = 10;
    [self.cacheProgressView mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(4);
        make.left.mas_equalTo(self.currentTimeLabel.mas_right).offset(5);
        make.width.mas_equalTo(self.mas_width).offset(-110);
        make.top.mas_equalTo(self.mas_top).mas_offset(29);
    }];
    self.cacheProgressView.layer.cornerRadius = 2;
    [self.totalTimeLabel mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.mas_equalTo(20);
        make.width.mas_equalTo(40);
        make.right.mas_equalTo(self.mas_right).offset(-10);
        make.top.mas_equalTo(self.mas_top).mas_offset(20);
    }];
    [self.playButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.width.mas_equalTo(64);
        make.centerX.equalTo(self);
        make.top.mas_equalTo(self.progressView.mas_bottom).mas_offset(10);
    }];
    [self.nextButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.width.mas_equalTo(48);
        make.left.mas_equalTo(self.playButton.mas_right).offset(20);
        make.centerY.equalTo(self.playButton);
    }];
    [self.prevButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.width.mas_equalTo(48);
        make.right.mas_equalTo(self.playButton.mas_left).offset(-20);
        make.centerY.equalTo(self.playButton);
    }];
    [self.loopButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.width.mas_equalTo(30);
        make.right.mas_equalTo(self.totalTimeLabel.mas_left).offset(-10);
        make.centerY.equalTo(self.playButton);
    }];
    [self.volumeButton mas_makeConstraints:^(MASConstraintMaker* make) {
        make.height.width.mas_equalTo(30);
        make.left.mas_equalTo(self.currentTimeLabel.mas_right).offset(10);
        make.centerY.equalTo(self.playButton);
    }];
    self.isPause = YES;
    self.isLoop = NO;
}

- (UIButton*)playButton {
    if (_playButton == nil) {
        _playButton = [Button new];
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

- (UIButton*)volumeButton {
    if (_volumeButton == nil) {
        _volumeButton = [UIButton new];
        [_volumeButton setImage:[UIImage imageNamed:@"volume"] forState:UIControlStateNormal];
        _volumeButton.imageView.contentMode = UIViewContentModeScaleAspectFit;
        [_volumeButton addTarget:self
                          action:@selector(volumeClick)
                forControlEvents:UIControlEventTouchUpInside];
    }
    return _volumeButton;
}

- (UIProgressView*)cacheProgressView {
    if (_cacheProgressView == nil) {
        _cacheProgressView = [UIProgressView new];
        _cacheProgressView.trackTintColor = [UIColor whiteColor];
        _cacheProgressView.progressTintColor = [[UIColor alloc] initWithRed:172.0f / 255.0f
                                                                      green:32.0f / 255.0f
                                                                       blue:219.0f / 255.0f
                                                                      alpha:1.f];
        _cacheProgressView.layer.cornerRadius = 2;
        _cacheProgressView.layer.masksToBounds = YES;
    }
    return _cacheProgressView;
}

- (UISlider*)progressView {
    if (_progressView == nil) {
        _progressView = [UISlider new];
        _progressView.maximumValue = 100.f;
        _progressView.minimumValue = 0.f;
        _progressView.minimumTrackTintColor = [[UIColor alloc] initWithRed:26.0f / 255.0f
                                                                     green:109.0f / 255.0f
                                                                      blue:1.f
                                                                     alpha:1.f];
        _progressView.maximumTrackTintColor = [UIColor clearColor];
        [_progressView addTarget:self
                          action:@selector(progressChanged)
                forControlEvents:UIControlEventValueChanged];
        [_progressView addTarget:self
                          action:@selector(seekDone)
                forControlEvents:UIControlEventTouchUpInside];
        _progressView.layer.cornerRadius = 10;
        _progressView.layer.masksToBounds = YES;
        UIImage* image = [self imageWithSize:CGSizeMake(12.0, 12.0)
                                       color:[[UIColor alloc] initWithRed:200.0f / 255.0f
                                                                    green:50.0f / 255.0f
                                                                     blue:219.0f / 255.0f
                                                                    alpha:1.f]
                                cornerRadius:60.0];
        [_progressView setThumbImage:image forState:UIControlStateNormal];
        [_progressView setThumbImage:image forState:UIControlStateHighlighted];
    }
    return _progressView;
}

- (UIImage*)imageWithSize:(CGSize)size color:(UIColor*)color cornerRadius:(CGFloat)cornerRadius {
    UIGraphicsBeginImageContextWithOptions(size, NO, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();

    CGRect bound = CGRectMake(0, 0, size.width, size.height);

    UIBezierPath* path = [UIBezierPath bezierPathWithRoundedRect:bound cornerRadius:cornerRadius];
    [path addClip];

    CGContextSetFillColorWithColor(context, color.CGColor);
    CGContextFillRect(context, bound);

    CGContextSetStrokeColorWithColor(context, [UIColor colorWithWhite:0.85 alpha:1].CGColor);

    UIBezierPath* border = [UIBezierPath bezierPathWithRoundedRect:CGRectInset(bound, 0.25, 0.25)
                                                      cornerRadius:cornerRadius];
    [border setLineWidth:0.5];
    [border stroke];

    UIImage* image = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();

    return image;
}

- (UILabel*)currentTimeLabel {
    if (_currentTimeLabel == nil) {
        _currentTimeLabel = [[UILabel alloc] initWithFrame:CGRectZero];
        _currentTimeLabel.text = [self timeString:0];
        _currentTimeLabel.textAlignment = NSTextAlignmentCenter;
        _currentTimeLabel.font = [UIFont systemFontOfSize:12];
        _currentTimeLabel.textColor = [UIColor whiteColor];
    }
    return _currentTimeLabel;
}

- (UILabel*)totalTimeLabel {
    if (_totalTimeLabel == nil) {
        _totalTimeLabel = [[UILabel alloc] initWithFrame:CGRectZero];
        _totalTimeLabel.text = [self timeString:0];
        _totalTimeLabel.textAlignment = NSTextAlignmentCenter;
        _totalTimeLabel.font = [UIFont systemFontOfSize:12];
        _totalTimeLabel.textColor = [UIColor whiteColor];
    }
    return _totalTimeLabel;
}

- (void)progressChanged {
    self.currentTimeLabel.text = [self timeString:self.progressView.value];
    !self.onSeekClick ?: self.onSeekClick(self.progressView.value);
}

- (void)seekDone {
    !self.onSeekDone ?: self.onSeekDone(self.progressView.value);
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

- (void)volumeClick {
    self.isMute = !self.isMute;
    !self.onSetMute ?: self.onSetMute(self.isMute);
    if (!self.isMute) {
        [self.volumeButton setImage:[UIImage imageNamed:@"volume"] forState:UIControlStateNormal];
    } else {
        [self.volumeButton setImage:[UIImage imageNamed:@"mute"] forState:UIControlStateNormal];
    }
}

- (void)setIsLoop:(BOOL)isLoop {
    _isLoop = isLoop;
    if (isLoop) {
        [self.loopButton setImage:[UIImage imageNamed:@"loop"] forState:UIControlStateNormal];
    } else {
        [self.loopButton setImage:[UIImage imageNamed:@"stop_loop"] forState:UIControlStateNormal];
    }
}

- (void)setIsPause:(BOOL)isPause {
    _isPause = isPause;
    if (_isPause) {
        [self.playButton setImage:[UIImage imageNamed:@"play_icon"] forState:UIControlStateNormal];
    } else {
        [self.playButton setImage:[UIImage imageNamed:@"pause_icon"] forState:UIControlStateNormal];
    }
    !self.onPlayClick ?: self.onPlayClick(!isPause);
}

- (void)updateCurrentTime:(NSTimeInterval)value {
    self.currentTime = value;
}

- (void)updateTotalTime:(NSTimeInterval)value {
    self.totalTime = value;
}

- (void)setTotalTime:(NSTimeInterval)totalTime {
    _totalTime = totalTime;
    self.progressView.maximumValue = _totalTime;
    self.totalTimeLabel.text = [self timeString:_totalTime];
}

- (void)setCurrentTime:(NSTimeInterval)currentTime {
    _currentTime = currentTime;
    self.progressView.value = currentTime;
    self.currentTimeLabel.text = [self timeString:_currentTime];
}

- (void)updateCacheTime:(NSTimeInterval)value {
    if (_totalTime != 0.0) {
        double process = value / _totalTime;
        if (fabs(process - 1.0) < 0.01) {
            process = 1.0;
        }
        self.cacheProgressView.progress = process;
    }
}

- (NSString*)timeString:(NSTimeInterval)time {
    if (time < 0) {
        time = 0;
    } else if (time > self.totalTime) {
        time = self.totalTime;
    }
    int hour = time / 3600.0;
    time -= hour * 3600.0;
    int mi = time / 60.0;
    int seconds = round(time - (mi * 60.0f));
    if (hour > 0) {
        return [NSString stringWithFormat:@"%02d:%02d:%02d", hour, mi, seconds];
    }
    return [NSString stringWithFormat:@"%02d:%02d", mi, seconds];
}

@end
