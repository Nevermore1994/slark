//
//  SlarkPlayer.h
//  slark
//
//  Created by Nevermore
//  
//  SlarkPlayer - iOS media player framework
//

#pragma once

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CMTime.h>

/**
 * Player state definitions
 */
typedef NS_ENUM(NSUInteger, SlarkPlayerState) {
    SlarkPlayerStateNotInited,      // Not initialized
    SlarkPlayerStateInitializing,   // Initializing
    SlarkPlayerStatePrepared,       // Ready for playback
    SlarkPlayerStateBuffering,      // Loading content
    SlarkPlayerStateReady,          // Ready to play
    SlarkPlayerStatePlaying,        // Currently playing
    SlarkPlayerStatePause,          // Paused
    SlarkPlayerStateStop,           // Stopped
    SlarkPlayerStateError,          // Error occurred
    SlarkPlayerStateCompleted,      // Playback finished
    SlarkPlayerStateUnknown         // Unknown state
};

/**
 * Player event types
 */
typedef NS_ENUM(NSUInteger, SlarkPlayerEvent) {
    SlarkPlayerEventFirstFrameRendered,  // First frame displayed
    SlarkPlayerEventSeekDone,           // Seek completed
    SlarkPlayerEventPlayEnd,            // Playback ended
    SlarkPlayerEventUpdateCacheTime,    // Cache updated
    SlarkPlayerEventOnError,            // Error occurred
};


typedef NS_ENUM(NSUInteger, SlarkPlayerErrorCode) {
    SlarkPlayerErrorFileError = 1000,
    SlarkPlayerErrorNetWorkError,
    SlarkPlayerErrorNotSupportFormat = 2000,
    SlarkPlayerErrorDemuxError,
    SlarkPlayerErrorDecodeError,
    SlarkPlayerErrorRenderError,
};

/**
 * Observer protocol for player notifications
 */
@protocol ISlarkPlayerObserver <NSObject>
// Time update callback
- (void)notifyTime:(NSString*) playerId time:(double) time;
// State change callback
- (void)notifyState:(NSString*) playerId state:(SlarkPlayerState) state;
// Event callback
- (void)notifyEvent:(NSString*) playerId event:(SlarkPlayerEvent) event value:(NSString*) value;
@end

/**
 * Main media player class
 */
@interface SlarkPlayer : NSObject

// Observer delegate
@property (nonatomic, weak) id<ISlarkPlayerObserver> delegate;

// Initialize with file path
- (instancetype)init:(NSString*) path;

// Initialize with file path and time range
- (instancetype)initWithTimeRange:(NSString*) path range:(CMTimeRange)range;

// Prepare for playback
- (void)prepare;

// Start playback
- (void)play;

// Pause playback
- (void)pause;

// Stop playback
- (void)stop;

// Seek to time position
- (void)seek:(double) seekToTime;

// Seek with accuracy option
- (void)seek:(double) seekToTime isAccurate:(BOOL)isAccurate;

// Enable/disable loop playback
- (void)setLoop:(BOOL) isLoop;

// Set volume (0.0-100.0)
- (void)setVolume:(float) volume;

// Mute/unmute audio
- (void)setMute:(BOOL) isMute;

// Get total media duration
- (CMTime)totalDuration;

// Get current playback time
- (CMTime)currentTime;

// Get current player state
- (SlarkPlayerState)state;

// Get unique player ID
- (NSString*)playerId;

@end
