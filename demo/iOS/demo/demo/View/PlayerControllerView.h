//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#import <UIKit/UIKit.h>

#ifndef PlayerControlView_h
#define PlayerControlView_h

@interface PlayerControllerView : UIView

@property (nonatomic, copy) void (^onPlayClick)(BOOL isPlaying);
@property (nonatomic, copy) void (^onSeekClick)(double time);
@property (nonatomic, copy) void (^onSeekDone)(double time);
@property (nonatomic, copy) void (^onPrevClick)(void);
@property (nonatomic, copy) void (^onNextClick)(void);
@property (nonatomic, copy) void (^onSetLoopClick)(BOOL loop);
@property (nonatomic, copy) void (^onSetMute)(BOOL mute);
@property (nonatomic, copy) void (^onSpeedChange)(float speed);
@property (nonatomic, copy) void (^onBackClick)(void);
@property (nonatomic, copy) void (^onRotateClick)(void);
@property (nonatomic, copy) void (^onRenderBackendChange)(BOOL useMetal);
@property (nonatomic, copy) void (^onVolumeChange)(float volume);
@property (nonatomic, readonly) BOOL isPause;

- (void)setIsPause:(BOOL)pause;
- (void)updateCurrentTime:(NSTimeInterval)value;
- (void)updateTotalTime:(NSTimeInterval)value;
- (void)updateCacheTime:(NSTimeInterval)value;
- (void)updateVolume:(float)volume;

@end

#endif /* PlayerControlView_h */
