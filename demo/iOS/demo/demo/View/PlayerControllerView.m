//
//  PlayerController.m
//  demo
//


#import "PlayerControllerView.h"
#import "Masonry.h"

@interface Button : UIButton

@end

@implementation Button

- (CGRect)imageRectForContentRect:(CGRect)bounds{
    return CGRectMake(0.0, 0.0, self.frame.size.width, self.frame.size.height);
}

@end

@interface PlayerControllerView()
@property (nonatomic, assign) BOOL isPause;
@property (nonatomic, assign) BOOL isLoop;
@property (nonatomic, strong) Button* playButton;
@property (nonatomic, strong) UIButton* prevButton;
@property (nonatomic, strong) UIButton* nextButton;
@property (nonatomic, strong) UIButton* loopButton;
@property (nonatomic, strong) UIButton* volumeButton;
@property (nonatomic, strong) UISlider* progressView;
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
    [self addSubview:self.progressView];
    [self addSubview:self.currentTimeLabel];
    [self addSubview:self.totalTimeLabel];
    [self.currentTimeLabel mas_makeConstraints:^(MASConstraintMaker *make) {
        make.height.mas_equalTo(20);
        make.width.mas_equalTo(40);
        make.left.mas_equalTo(self.mas_left).offset(10);
        make.top.mas_equalTo(self.mas_top).mas_offset(20);
    }];
    [self.progressView mas_makeConstraints:^(MASConstraintMaker *make) {
        make.height.mas_equalTo(20);
        make.left.mas_equalTo(self.currentTimeLabel.mas_right).offset(5);
        make.width.mas_equalTo(self.mas_width).offset(-110);
        make.top.mas_equalTo(self.mas_top).mas_offset(20);
    }];
    [self.totalTimeLabel mas_makeConstraints:^(MASConstraintMaker *make) {
        make.height.mas_equalTo(20);
        make.width.mas_equalTo(40);
        make.right.mas_equalTo(self.mas_right).offset(-10);
        make.top.mas_equalTo(self.mas_top).mas_offset(20);
    }];
    [self.playButton mas_makeConstraints:^(MASConstraintMaker *make) {
        make.height.width.mas_equalTo(64);
        make.centerX.equalTo(self);
        make.top.mas_equalTo(self.progressView.mas_bottom).mas_offset(10);
    }];
    [self.nextButton mas_makeConstraints:^(MASConstraintMaker *make) {
        make.height.width.mas_equalTo(48);
        make.left.mas_equalTo(self.playButton.mas_right).offset(20);
        make.centerY.equalTo(self.playButton);
    }];
    [self.prevButton mas_makeConstraints:^(MASConstraintMaker *make) {
        make.height.width.mas_equalTo(48);
        make.right.mas_equalTo(self.playButton.mas_left).offset(-20);
        make.centerY.equalTo(self.playButton);
    }];
    [self.loopButton mas_makeConstraints:^(MASConstraintMaker *make) {
        make.height.width.mas_equalTo(30);
        make.right.mas_equalTo(self.totalTimeLabel.mas_left).offset(-10);
        make.centerY.equalTo(self.playButton);
    }];
    [self.volumeButton mas_makeConstraints:^(MASConstraintMaker *make) {
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
        [_playButton addTarget:self action:@selector(playClick) forControlEvents:UIControlEventTouchUpInside];
    }
    return _playButton;
}

- (UIButton*)prevButton {
    if (_prevButton == nil) {
        _prevButton = [UIButton new];
        [_prevButton setImage:[UIImage imageNamed:@"prev"] forState:UIControlStateNormal];
        _prevButton.imageView.contentMode = UIViewContentModeScaleAspectFit;
        [_prevButton addTarget:self action:@selector(prevClick) forControlEvents:UIControlEventTouchUpInside];
    }
    return _prevButton;
}

- (UIButton*)nextButton {
    if (_nextButton == nil) {
        _nextButton = [UIButton new];
        [_nextButton setImage:[UIImage imageNamed:@"next"] forState:UIControlStateNormal];
        _nextButton.imageView.contentMode = UIViewContentModeScaleAspectFit;
        [_nextButton addTarget:self action:@selector(nextClick) forControlEvents:UIControlEventTouchUpInside];
    }
    return _nextButton;
}

- (UIButton*)loopButton {
    if (_loopButton == nil) {
        _loopButton = [UIButton new];
        [_loopButton setImage:[UIImage imageNamed:@"loop"] forState:UIControlStateNormal];
        _loopButton.imageView.contentMode = UIViewContentModeScaleAspectFit;
        [_loopButton addTarget:self action:@selector(loopClick) forControlEvents:UIControlEventTouchUpInside];
    }
    return _loopButton;
}

- (UIButton*)volumeButton {
    if (_volumeButton == nil) {
        _volumeButton = [UIButton new];
        [_volumeButton setImage:[UIImage imageNamed:@"volume"] forState:UIControlStateNormal];
        _volumeButton.imageView.contentMode = UIViewContentModeScaleAspectFit;
        //[_volumeButton addTarget:self action:@selector(onClick) forControlEvents:UIControlEventTouchUpInside];
    }
    return _volumeButton;
}

- (UISlider*)progressView {
    if (_progressView == nil) {
        _progressView = [UISlider new];
        _progressView.maximumValue = 100.f;
        _progressView.minimumValue = 0.f;
        _progressView.minimumTrackTintColor = [[UIColor alloc] initWithRed:26.0f / 255.0f  green:109.0f / 255.0f  blue:1.f alpha:1.f];
        _progressView.maximumTrackTintColor = [[UIColor alloc] initWithRed:172.0f / 255.0f  green:32.0f / 255.0f blue:219.0f / 255.0f alpha:1.f];
        [_progressView addTarget:self action:@selector(progressChanged) forControlEvents:UIControlEventValueChanged];
        _progressView.layer.cornerRadius = 10;
        _progressView.layer.masksToBounds = YES;
        UIImage* image = [self imageWithSize:CGSizeMake(12.0, 12.0) color:[[UIColor alloc] initWithRed:200.0f / 255.0f  green:50.0f / 255.0f blue:219.0f / 255.0f alpha:1.f] cornerRadius:60.0];
        [_progressView setThumbImage:image forState:UIControlStateNormal];
        [_progressView setThumbImage:image forState:UIControlStateHighlighted];
    }
    return _progressView;
}

- (UIImage *)imageWithSize:(CGSize)size color:(UIColor *)color cornerRadius:(CGFloat)cornerRadius {

    UIGraphicsBeginImageContextWithOptions(size, NO, 0);
    CGContextRef context = UIGraphicsGetCurrentContext();
    
    CGRect bound = CGRectMake(0, 0, size.width, size.height);
    
    UIBezierPath *path = [UIBezierPath bezierPathWithRoundedRect:bound cornerRadius:cornerRadius];
    [path addClip];
    
    CGContextSetFillColorWithColor(context, color.CGColor);
    CGContextFillRect(context, bound);
    
    CGContextSetStrokeColorWithColor(context, [UIColor colorWithWhite:0.85 alpha:1].CGColor);

    UIBezierPath *border = [UIBezierPath bezierPathWithRoundedRect:CGRectInset(bound, 0.25, 0.25) cornerRadius:cornerRadius];
    [border setLineWidth:0.5];
    [border stroke];
    
    UIImage *image = UIGraphicsGetImageFromCurrentImageContext();
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
    !self.onPlayClick ?:self.onPlayClick(!isPause);
}

- (void)updateCurrentTime:(NSTimeInterval) value {
    self.currentTime = value;
}

- (void)updateTotalTime:(NSTimeInterval) value {
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
    if (hour > 0)
        return [NSString stringWithFormat:@"%02d:%02d:%02d", hour, mi, seconds];
    return [NSString stringWithFormat:@"%02d:%02d", mi, seconds];
}
@end
