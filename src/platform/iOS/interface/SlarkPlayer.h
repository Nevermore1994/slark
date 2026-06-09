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
    SlarkPlayerErrorSuccess = 0,
    SlarkPlayerErrorOutOfMemory = 1,
    SlarkPlayerErrorOutOfRange = 2,
    SlarkPlayerErrorNullPointer = 3,
    SlarkPlayerErrorBufferOverflow = 4,
    SlarkPlayerErrorInvalidOperation = 5,
    SlarkPlayerErrorInvalidArgument = 6,
    SlarkPlayerErrorSizeTooLarge = 7,
    SlarkPlayerErrorEmptyData = 8,
    SlarkPlayerErrorNotFound = 9,
    SlarkPlayerErrorCancelled = 10,
    SlarkPlayerErrorIOError = 100,
    SlarkPlayerErrorEndOfFile = 101,
    SlarkPlayerErrorPermissionDenied = 102,
    SlarkPlayerErrorFileNotFound = 103,
    SlarkPlayerErrorFileAlreadyExists = 104,
    SlarkPlayerErrorStreamNotReady = 110,
    SlarkPlayerErrorStreamInvalidData = 111,
    SlarkPlayerErrorCodecNotFound = 200,
    SlarkPlayerErrorCodecError = 201,
    SlarkPlayerErrorCodecInputError = 202,
    SlarkPlayerErrorCodecInputTooLarge = 203,
    SlarkPlayerErrorCodecNotStarted = 204,
    SlarkPlayerErrorCodecNotProcessed = 205,
    SlarkPlayerErrorCodecAgain = 206,
    SlarkPlayerErrorCodecUnsupportedFormat = 207,
    SlarkPlayerErrorCodecResourceExhausted = 208,
    SlarkPlayerErrorCodecConfigInvalid = 209,
    SlarkPlayerErrorNetworkError = 300,
    SlarkPlayerErrorConnectionFailed = 301,
    SlarkPlayerErrorConnectionTimeout = 302,
    SlarkPlayerErrorConnectionReset = 303,
    SlarkPlayerErrorConnectionRefused = 304,
    SlarkPlayerErrorDnsResolveFailed = 305,
    SlarkPlayerErrorSocketError = 306,
    SlarkPlayerErrorSocketClosed = 307,
    SlarkPlayerErrorWouldBlock = 308,
    SlarkPlayerErrorInvalidSocket = 309,
    SlarkPlayerErrorBrokenPipe = 310,
    SlarkPlayerErrorHttpError = 400,
    SlarkPlayerErrorHttpTimeout = 401,
    SlarkPlayerErrorHttpRedirectError = 402,
    SlarkPlayerErrorHttpRedirectTooMany = 403,
    SlarkPlayerErrorHttpResponseInvalid = 404,
    SlarkPlayerErrorHttpChunkedError = 405,
    SlarkPlayerErrorHttpSslError = 406,
    SlarkPlayerErrorHttpSslHandshakeFailed = 407,
    SlarkPlayerErrorMediaOpenFailed = 500,
    SlarkPlayerErrorMediaEndOfFile = 501,
    SlarkPlayerErrorMediaHeaderParsed = 502,
    SlarkPlayerErrorMediaInfoChanged = 503,
    SlarkPlayerErrorMediaFpsParsed = 504,
    SlarkPlayerErrorMediaInvalidData = 505,
    SlarkPlayerErrorPipelineOpenFailed = 600,
    SlarkPlayerErrorPipelineFormatNotSupported = 601,
    SlarkPlayerErrorPipelineDemuxError = 602,
    SlarkPlayerErrorPipelineDecodeError = 603,
    SlarkPlayerErrorPipelineRenderError = 604,
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
@property (nonatomic, assign) BOOL isLoop;
@property (nonatomic, assign) BOOL isMute;
@property (nonatomic, assign) CGFloat volume;
@property (nonatomic, assign) SlarkPlayerRotation rotation;
@property (nonatomic, assign, readonly) SlarkPlayerState currentState;
@property (nonatomic, assign, readonly) NSString* playerId;
@property (nonatomic, assign, readonly) CMTime totalDuration;
@property (nonatomic, assign, readonly) CMTime currentTime;
@property (nonatomic, weak) id<ISlarkPlayerObserver> delegate;

- (instancetype)init:(NSString*)path;

- (instancetype)initWithTimeRange:(NSString*)path range:(CMTimeRange)range;

- (void)prepare;

- (void)play;

- (void)pause;

- (void)stop;

- (void)seek:(double)seekToTime;

- (void)seek:(double)seekToTime isAccurate:(BOOL)isAccurate;

@end