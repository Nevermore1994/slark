//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
//  SlarkPlayer - iOS media player framework
//

#pragma once

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CMTime.h>
typedef NS_ENUM(NSUInteger, SlarkPlayerState) {
    SlarkPlayerStateNotInited,
    SlarkPlayerStateInitializing,
    SlarkPlayerStatePrepared,
    SlarkPlayerStateBuffering,
    SlarkPlayerStateReady,
    SlarkPlayerStatePlaying,
    SlarkPlayerStatePause,
    SlarkPlayerStateStop,
    SlarkPlayerStateError,
    SlarkPlayerStateCompleted,
    SlarkPlayerStateUnknown
};

typedef NS_ENUM(NSUInteger, SlarkPlayerEvent) {
    SlarkPlayerEventFirstFrameRendered,
    SlarkPlayerEventSeekDone,
    SlarkPlayerEventPlayEnd,
    SlarkPlayerEventUpdateCacheTime,
    SlarkPlayerEventOnError,
};

typedef NS_ENUM(NSUInteger, SlarkPlayerErrorCode) {
    SlarkPlayerErrorCodeSuccess = 0,
    SlarkPlayerErrorCodeOutOfMemory = 1,
    SlarkPlayerErrorCodeOutOfRange = 2,
    SlarkPlayerErrorCodeNullPointer = 3,
    SlarkPlayerErrorCodeBufferOverflow = 4,
    SlarkPlayerErrorCodeInvalidOperation = 5,
    SlarkPlayerErrorCodeInvalidArgument = 6,
    SlarkPlayerErrorCodeSizeTooLarge = 7,
    SlarkPlayerErrorCodeEmptyData = 8,
    SlarkPlayerErrorCodeNotFound = 9,
    SlarkPlayerErrorCodeCancelled = 10,
    SlarkPlayerErrorCodeIOError = 100,
    SlarkPlayerErrorCodeEndOfFile = 101,
    SlarkPlayerErrorCodePermissionDenied = 102,
    SlarkPlayerErrorCodeFileNotFound = 103,
    SlarkPlayerErrorCodeFileAlreadyExists = 104,
    SlarkPlayerErrorCodeStreamNotReady = 110,
    SlarkPlayerErrorCodeStreamInvalidData = 111,
    SlarkPlayerErrorCodeCodecNotFound = 200,
    SlarkPlayerErrorCodeCodecError = 201,
    SlarkPlayerErrorCodeCodecInputError = 202,
    SlarkPlayerErrorCodeCodecInputTooLarge = 203,
    SlarkPlayerErrorCodeCodecNotStarted = 204,
    SlarkPlayerErrorCodeCodecNotProcessed = 205,
    SlarkPlayerErrorCodeCodecAgain = 206,
    SlarkPlayerErrorCodeCodecUnsupportedFormat = 207,
    SlarkPlayerErrorCodeCodecResourceExhausted = 208,
    SlarkPlayerErrorCodeCodecConfigInvalid = 209,
    SlarkPlayerErrorCodeNetworkError = 300,
    SlarkPlayerErrorCodeConnectionFailed = 301,
    SlarkPlayerErrorCodeConnectionTimeout = 302,
    SlarkPlayerErrorCodeConnectionReset = 303,
    SlarkPlayerErrorCodeConnectionRefused = 304,
    SlarkPlayerErrorCodeDnsResolveFailed = 305,
    SlarkPlayerErrorCodeSocketError = 306,
    SlarkPlayerErrorCodeSocketClosed = 307,
    SlarkPlayerErrorCodeWouldBlock = 308,
    SlarkPlayerErrorCodeInvalidSocket = 309,
    SlarkPlayerErrorCodeBrokenPipe = 310,
    SlarkPlayerErrorCodeHttpError = 400,
    SlarkPlayerErrorCodeHttpTimeout = 401,
    SlarkPlayerErrorCodeHttpRedirectError = 402,
    SlarkPlayerErrorCodeHttpRedirectTooMany = 403,
    SlarkPlayerErrorCodeHttpResponseInvalid = 404,
    SlarkPlayerErrorCodeHttpChunkedError = 405,
    SlarkPlayerErrorCodeHttpSslError = 406,
    SlarkPlayerErrorCodeHttpSslHandshakeFailed = 407,
    SlarkPlayerErrorCodeMediaOpenFailed = 500,
    SlarkPlayerErrorCodeMediaEndOfFile = 501,
    SlarkPlayerErrorCodeMediaHeaderParsed = 502,
    SlarkPlayerErrorCodeMediaInfoChanged = 503,
    SlarkPlayerErrorCodeMediaFpsParsed = 504,
    SlarkPlayerErrorCodeMediaInvalidData = 505,
    SlarkPlayerErrorCodePipelineOpenFailed = 600,
    SlarkPlayerErrorCodePipelineFormatNotSupported = 601,
    SlarkPlayerErrorCodePipelineDemuxError = 602,
    SlarkPlayerErrorCodePipelineDecodeError = 603,
    SlarkPlayerErrorCodePipelineRenderError = 604
};

typedef NS_ENUM(NSUInteger, SlarkPlayerRotation) {
    SlarkPlayerRotation_0,
    SlarkPlayerRotation_90,
    SlarkPlayerRotation_180,
    SlarkPlayerRotation_270,
};

/**
 * Observer protocol for player notifications
 */
@protocol ISlarkPlayerObserver <NSObject>
- (void)notifyTime:(NSString*)playerId time:(double)time;
- (void)notifyState:(NSString*)playerId state:(SlarkPlayerState)state;
- (void)notifyEvent:(NSString*)playerId event:(SlarkPlayerEvent)event value:(NSString*)value;
@end

@interface SlarkPlayer : NSObject
@property(nonatomic, assign) BOOL isLoop;
@property(nonatomic, assign) BOOL isMute;
@property(nonatomic, assign) CGFloat volume;
@property(nonatomic, assign) SlarkPlayerRotation rotation;
@property(nonatomic, assign, readonly) SlarkPlayerState currentState;
@property(nonatomic, assign, readonly) NSString* playerId;
@property(nonatomic, assign, readonly) CMTime totalDuration;
@property(nonatomic, assign, readonly) CMTime currentTime;
@property(nonatomic, assign, readonly) BOOL isSeekable;
@property(nonatomic, weak) id<ISlarkPlayerObserver> delegate;

- (instancetype)init:(NSString*)path;

- (instancetype)initWithTimeRange:(NSString*)path range:(CMTimeRange)range;

- (void)prepare;

- (void)play;

- (void)pause;

- (void)stop;

- (void)seek:(double)seekToTime;

- (void)seek:(double)seekToTime isAccurate:(BOOL)isAccurate;

@end