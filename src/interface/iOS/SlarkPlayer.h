//
//  slark.h
//  slark
//
//  Created by Nevermore
//

#pragma once

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CMTime.h>
#import <memory>

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
} SlarkPlayerState;


typedef enum : NSUInteger {
    PlayerEventFirstFrameRendered,
    PlayerEventSeekDone,
    PlayerEventPlayEnd,
    PlayerEventOnError,
} SlarkPlayerEvent;

@protocol ISlarkPlayerObserver <NSObject>
- (void)notifyTime:(NSString*) playerId time:(double) time;
- (void)notifyState:(NSString*) playerId state:(SlarkPlayerState) state;
- (void)notifyEvent:(NSString*) playerId event:(SlarkPlayerEvent) event value:(NSString*) value;
@end

namespace slark {

struct IVideoRender;

}

@interface SlarkPlayer : NSObject
@property (nonatomic, weak) id<ISlarkPlayerObserver> delegate;

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

- (SlarkPlayerState)state;

- (NSString*)playerId;

- (CVPixelBufferRef)requestRender;

- (void)setRenderImpl:(std::weak_ptr<slark::IVideoRender>) ptr;
@end
