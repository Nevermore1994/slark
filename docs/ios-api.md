# Slark iOS API Documentation

Slark iOS SDK provides complete media playback functionality, supporting both local file and network streaming media playback.

## Table of Contents

- [Core Classes](#core-classes)
  - [SlarkPlayer](#slarkplayer)
  - [SlarkViewController](#slarkviewcontroller)
- [Protocols](#protocols)
  - [ISlarkPlayerObserver](#islarkplayerobserver)
- [Enumerations](#enumerations)
  - [SlarkPlayerState](#slarkplayerstate)
  - [SlarkPlayerEvent](#slarkplayerevent)
- [Usage Examples](#usage-examples)

## Core Classes

### SlarkPlayer

The main media player class that provides complete audio and video playback control functionality.

#### Properties

```objc
@property (nonatomic, weak) id<ISlarkPlayerObserver> delegate;
```
- **Description**: Player observer delegate for receiving playback event notifications
- **Type**: `id<ISlarkPlayerObserver>`

#### Initialization Methods

```objc
- (instancetype)init:(NSString*) path;
```
- **Description**: Initialize player with file path
- **Parameters**: 
  - `path`: Media file path (local path or network URL)
- **Return Value**: SlarkPlayer instance

```objc
- (instancetype)initWithTimeRange:(NSString*) path range:(CMTimeRange)range;
```
- **Description**: Initialize player with file path and time range
- **Parameters**: 
  - `path`: Media file path
  - `range`: Playback time range
- **Return Value**: SlarkPlayer instance

#### Playback Control Methods

```objc
- (void)prepare;
```
- **Description**: Prepare for playback, must be called before playing
- **Note**: Asynchronous operation, status changes will be notified through delegate upon completion

```objc
- (void)play;
```
- **Description**: Start playing media

```objc
- (void)pause;
```
- **Description**: Pause playback

```objc
- (void)stop;
```
- **Description**: Stop playback and release media resources

#### Playback Position Control

```objc
- (void)seek:(double) seekToTime;
```
- **Description**: Fast forward/rewind to specified time position
- **Parameters**: 
  - `seekToTime`: Target time position (seconds)

```objc
- (void)seek:(double) seekToTime isAccurate:(BOOL)isAccurate;
```
- **Description**: Fast forward/rewind to specified time position with optional accurate positioning
- **Parameters**: 
  - `seekToTime`: Target time position (seconds)
  - `isAccurate`: Whether to use accurate positioning

#### Audio Control

```objc
- (void)setVolume:(float) volume;
```
- **Description**: Set volume
- **Parameters**: 
  - `volume`: Volume value (0.0-100.0)

```objc
- (void)setMute:(BOOL) isMute;
```
- **Description**: Set mute state
- **Parameters**: 
  - `isMute`: Whether to mute

#### Playback Mode Control

```objc
- (void)setLoop:(BOOL) isLoop;
```
- **Description**: Set loop playback mode
- **Parameters**: 
  - `isLoop`: Whether to loop playback

#### Information Retrieval Methods

```objc
- (CMTime)totalDuration;
```
- **Description**: Get total media duration
- **Return Value**: Total duration (CMTime)

```objc
- (CMTime)currentTime;
```
- **Description**: Get current playback time
- **Return Value**: Current playback time (CMTime)

```objc
- (SlarkPlayerState)state;
```
- **Description**: Get current player state
- **Return Value**: Player state enumeration value

```objc
- (NSString*)playerId;
```
- **Description**: Get player unique identifier
- **Return Value**: Player ID string

### SlarkViewController

Video playback view controller that provides video rendering interface.

#### Properties

```objc
@property(nonatomic, strong, readonly) SlarkPlayer* player;
```
- **Description**: Associated player instance
- **Type**: `SlarkPlayer*`
- **Access**: Read-only

#### Initialization Methods

```objc
- (instancetype)initWithPlayer:(CGRect) frame player:(SlarkPlayer*) player;
```
- **Description**: Initialize view controller with player instance and display area
- **Parameters**: 
  - `frame`: View display area
  - `player`: SlarkPlayer instance
- **Return Value**: SlarkViewController instance

## Protocols

### ISlarkPlayerObserver

Player observer protocol for receiving playback status and event notifications.

#### Methods

```objc
- (void)notifyTime:(NSString*) playerId time:(double) time;
```
- **Description**: Playback time update callback
- **Parameters**: 
  - `playerId`: Player ID
  - `time`: Current playback time (seconds)

```objc
- (void)notifyState:(NSString*) playerId state:(SlarkPlayerState) state;
```
- **Description**: Playback state change callback
- **Parameters**: 
  - `playerId`: Player ID
  - `state`: New playback state

```objc
- (void)notifyEvent:(NSString*) playerId event:(SlarkPlayerEvent) event value:(NSString*) value;
```
- **Description**: Playback event callback
- **Parameters**: 
  - `playerId`: Player ID
  - `event`: Event type
  - `value`: Event-related value

## Enumerations

### SlarkPlayerState

Player state enumeration.

```objc
typedef enum : NSUInteger {
    PlayerStateNotInited,      // Not initialized
    PlayerStateInitializing,   // Initializing
    PlayerStatePrepared,       // Prepared
    PlayerStateBuffering,      // Buffering
    PlayerStateReady,          // Ready to play
    PlayerStatePlaying,        // Playing
    PlayerStatePause,          // Paused
    PlayerStateStop,           // Stopped
    PlayerStateError,          // Error
    PlayerStateCompleted,      // Playback completed
    PlayerStateUnknown         // Unknown state
} SlarkPlayerState;
```

### SlarkPlayerEvent

Player event enumeration.

```objc
typedef enum : NSUInteger {
    PlayerEventFirstFrameRendered,  // First frame rendered
    PlayerEventSeekDone,           // Seek completed
    PlayerEventPlayEnd,            // Playback ended
    PlayerEventUpdateCacheTime,    // Cache updated
    PlayerEventOnError,            // Error occurred
} SlarkPlayerEvent;
```

## Usage Examples

### Basic Playback Flow

```objc
#import <SlarkPlayer.h>

@interface ViewController () <ISlarkPlayerObserver>
@property (nonatomic, strong) SlarkPlayer *player;
@property (nonatomic, strong) SlarkViewController *playerController;
@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    // 1. Create player
    self.player = [[SlarkPlayer alloc] init:@"https://example.com/video.mp4"];
    self.player.delegate = self;
    
    // 2. Create view controller
    self.playerController = [[SlarkViewController alloc] 
        initWithPlayer:self.view.bounds player:self.player];
    
    // 3. Add view controller
    [self addChildViewController:self.playerController];
    [self.view addSubview:self.playerController.view];
    
    // 4. Prepare for playback
    [self.player prepare];
}

// Playback control
- (void)playVideo {
    [self.player play];
}

- (void)pauseVideo {
    [self.player pause];
}

- (void)seekToTime:(double)time {
    [self.player seek:time];
}

- (void)setVolume:(float)volume {
    [self.player setVolume:volume];
}

#pragma mark - ISlarkPlayerObserver

- (void)notifyTime:(NSString*)playerId time:(double)time {
    // Update playback progress UI
    NSLog(@"Playback time: %.2f", time);
}

- (void)notifyState:(NSString*)playerId state:(SlarkPlayerState)state {
    switch (state) {
        case PlayerStatePlaying:
            NSLog(@"Playback started");
            break;
        case PlayerStatePause:
            NSLog(@"Playback paused");
            break;
        case PlayerStateCompleted:
            NSLog(@"Playback completed");
            break;
        case PlayerStateError:
            NSLog(@"Playback error");
            break;
        default:
            break;
    }
}

- (void)notifyEvent:(NSString*)playerId event:(SlarkPlayerEvent)event value:(NSString*)value {
    switch (event) {
        case PlayerEventFirstFrameRendered:
            NSLog(@"First frame rendered");
            break;
        case PlayerEventSeekDone:
            NSLog(@"Seek completed");
            break;
        default:
            break;
    }
}

@end
```

### Audio Playback Example

```objc
// Create audio player
SlarkPlayer *audioPlayer = [[SlarkPlayer alloc] init:@"path/to/audio.wav"];
audioPlayer.delegate = self;

// Set audio properties
[audioPlayer setVolume:80.0f];
[audioPlayer setLoop:YES];

// Start playback
[audioPlayer prepare];
[audioPlayer play];
```

### Network Streaming Media Playback

```objc
// HLS stream playback
SlarkPlayer *hlsPlayer = [[SlarkPlayer alloc] init:@"https://example.com/stream.m3u8"];
hlsPlayer.delegate = self;

[hlsPlayer prepare];
[hlsPlayer play];
```

## Important Notes

1. **Thread Safety**: All observer callbacks are executed on the main thread
2. **Memory Management**: Use weak references when setting delegates to avoid retain cycles
3. **Prepare Before Play**: Must call `prepare` method before performing playback operations
4. **View Lifecycle**: Call `stop` method at appropriate times to release resources
5. **Network Permissions**: Network playback requires configuring network access permissions in Info.plist

## Error Handling

When errors occur during playback, they are notified through the following ways:

1. Player state changes to `PlayerStateError`
2. Triggers `PlayerEventOnError` event
3. Error information is passed through the observer protocol's `notifyEvent` method

```objc
- (void)notifyEvent:(NSString*)playerId event:(SlarkPlayerEvent)event value:(NSString*)value {
    if (event == PlayerEventOnError) {
        NSLog(@"Playback error: %@", value);
        // Handle error logic
    }
}
```