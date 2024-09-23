//
//  slark.h
//  slark
//
//  Created by Nevermore
//

#pragma once

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CMTime.h>

typedef enum : NSUInteger {
    PlayerStateUnknown,
    PlayerStateInitializing,
    PlayerStateReady,
    PlayerStateBuffering,
    PlayerStatePlaying,
    PlayerStatePause,
    PlayerStateStop,
    PlayerStateError,
    PlayerStateCompleted,
} PlayerState;


typedef enum : NSUInteger {
    PlayerEventFirstFrameRendered,
    PlayerEventSeekDone,
    PlayerEventPlayEnd,
    PlayerEventOnError,
} PlayerEvent;

@protocol IPlayerObserver <NSObject>
- (void)notifyTime:(NSString*) playerId time:(double) time;
- (void)notifyState:(NSString*) playerId state:(PlayerState) state;
- (void)notifyEvent:(NSString*) playerId event:(PlayerEvent) event value:(NSString*) value;
@end

@interface Player : NSObject
@property (nonatomic, weak) id<IPlayerObserver> delegate;

- (instancetype)init:(NSString*) path;

- (instancetype)initWithTimeRange:(NSString*) path range:(CMTimeRange)range;

- (void)play;

- (void)pause;

- (void)stop;
 
- (void)seek:(double) seekToTime;

- (void)seek:(double) seekToTime isAccurate:(BOOL)isAccurate;

- (void)setLoop:(BOOL) isLoop;

- (void)setVolume:(float) volume;

- (void)setMute:(BOOL) isMute;

- (void)setRenderSize:(int) width height:(int) height;

- (CMTime)totalDuration;

- (CMTime)currentTime;

- (PlayerState)state;

@end
