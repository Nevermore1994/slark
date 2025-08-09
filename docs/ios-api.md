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
  - [SlarkPlayerErrorCode](#slarkplayererrorcode)
  - [SlarkPlayerRotation](#slarkplayerrotation)
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

```objc
@property(nonatomic, assign) BOOL isLoop;
```
- **Description**: Loop playback mode
- **Type**: `BOOL`

```objc
@property(nonatomic, assign) BOOL isMute;
```
- **Description**: Mute state
- **Type**: `BOOL`

```objc
@property(nonatomic, assign) CGFloat volume;
```
- **Description**: Volume control
- **Type**: `CGFloat`
- **Range**: 0.0 - 100.0

```objc
@property(nonatomic, assign) SlarkPlayerRotation rotation;
```
- **Description**: Video rotation angle
- **Type**: `SlarkPlayerRotation`

```objc
@property(nonatomic, assign, readonly) SlarkPlayerState currentState;
```
- **Description**: Current player state
- **Type**: `SlarkPlayerState`
- **Access**: Read-only

```objc
@property(nonatomic, assign, readonly) NSString* playerId;
```
- **Description**: Player unique identifier
- **Type**: `NSString*`
- **Access**: Read-only

```objc
@property(nonatomic, assign, readonly) CMTime totalDuration;
```
- **Description**: Total media duration
- **Type**: `CMTime`
- **Access**: Read-only

```objc
@property(nonatomic, assign, readonly) CMTime currentTime;
```
- **Description**: Current playback time
- **Type**: `CMTime`
- **Access**: Read-only

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
```

### SlarkPlayerEvent

Player event enumeration.

```objc
typedef NS_ENUM(NSUInteger, SlarkPlayerEvent) {
    SlarkPlayerEventFirstFrameRendered,  // First frame displayed
    SlarkPlayerEventSeekDone,           // Seek completed
    SlarkPlayerEventPlayEnd,            // Playback ended
    SlarkPlayerEventUpdateCacheTime,    // Cache updated
    SlarkPlayerEventOnError,            // Error occurred
};
```

### SlarkPlayerErrorCode

Player error code enumeration.

```objc
typedef NS_ENUM(NSUInteger, SlarkPlayerErrorCode) {
    SlarkPlayerErrorFileError = 1000,       // File error
    SlarkPlayerErrorNetWorkError,           // Network error
    SlarkPlayerErrorNotSupportFormat = 2000, // Format not supported
    SlarkPlayerErrorDemuxError,             // Demux error
    SlarkPlayerErrorDecodeError,            // Decode error
    SlarkPlayerErrorRenderError,            // Render error
};
```

### SlarkPlayerRotation

Video rotation enumeration.

```objc
typedef NS_ENUM(NSUInteger, SlarkPlayerRotation) {
    SlarkPlayerRotation_0,      // 0 degrees
    SlarkPlayerRotation_90,     // 90 degrees
    SlarkPlayerRotation_180,    // 180 degrees
    SlarkPlayerRotation_270,    // 270 degrees
};
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

- (void)setVolume:(CGFloat)volume {
    self.player.volume = volume;
}

- (void)setLoop:(BOOL)loop {
    self.player.isLoop = loop;
}

- (void)setMute:(BOOL)mute {
    self.player.isMute = mute;
}

- (void)setRotation:(SlarkPlayerRotation)rotation {
    self.player.rotation = rotation;
}

#pragma mark - ISlarkPlayerObserver

- (void)notifyTime:(NSString*)playerId time:(double)time {
    // Update playback progress UI
    NSLog(@"Playback time: %.2f", time);
    // Update progress bar on main thread
    dispatch_async(dispatch_get_main_queue(), ^{
        // Update UI components
    });
}

- (void)notifyState:(NSString*)playerId state:(SlarkPlayerState)state {
    dispatch_async(dispatch_get_main_queue(), ^{
        switch (state) {
            case SlarkPlayerStatePlaying:
                NSLog(@"Playback started");
                break;
            case SlarkPlayerStatePause:
                NSLog(@"Playback paused");
                break;
            case SlarkPlayerStateCompleted:
                NSLog(@"Playback completed");
                break;
            case SlarkPlayerStateError:
                NSLog(@"Playback error");
                break;
            default:
                break;
        }
    });
}

- (void)notifyEvent:(NSString*)playerId event:(SlarkPlayerEvent)event value:(NSString*)value {
    dispatch_async(dispatch_get_main_queue(), ^{
        switch (event) {
            case SlarkPlayerEventFirstFrameRendered:
                NSLog(@"First frame rendered");
                break;
            case SlarkPlayerEventSeekDone:
                NSLog(@"Seek completed");
                break;
            case SlarkPlayerEventOnError:
                NSLog(@"Playback error: %@", value);
                [self handlePlaybackError:value];
                break;
            default:
                break;
        }
    });
}

- (void)handlePlaybackError:(NSString*)errorValue {
    NSInteger errorCode = [errorValue integerValue];
    switch (errorCode) {
        case SlarkPlayerErrorNetWorkError:
            [self showErrorAlert:@"Network connection failed, please check network settings"];
            break;
        case SlarkPlayerErrorFileError:
            [self showErrorAlert:@"File cannot be opened or does not exist"];
            break;
        case SlarkPlayerErrorDecodeError:
            [self showErrorAlert:@"Video format not supported"];
            break;
        default:
            [self showErrorAlert:[NSString stringWithFormat:@"Playback error: %@", errorValue]];
            break;
    }
}

- (void)showErrorAlert:(NSString*)message {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Playback Error"
                                                                   message:message
                                                            preferredStyle:UIAlertControllerStyleAlert];
    UIAlertAction *okAction = [UIAlertAction actionWithTitle:@"OK"
                                                       style:UIAlertActionStyleDefault
                                                     handler:nil];
    [alert addAction:okAction];
    [self presentViewController:alert animated:YES completion:nil];
}

@end
```

### Audio Playback Example

```objc
// Create audio player
SlarkPlayer *audioPlayer = [[SlarkPlayer alloc] init:@"path/to/audio.wav"];
audioPlayer.delegate = self;

// Set audio properties
audioPlayer.volume = 80.0f;
audioPlayer.isLoop = YES;

// Start playback
[audioPlayer prepare];
[audioPlayer play];
```

### Time Range Playback Example

```objc
// Play specific time range (30 seconds starting from 60 seconds)
CMTime startTime = CMTimeMakeWithSeconds(60.0, 600);
CMTime duration = CMTimeMakeWithSeconds(30.0, 600);
CMTimeRange range = CMTimeRangeMake(startTime, duration);

SlarkPlayer *player = [[SlarkPlayer alloc] initWithTimeRange:@"https://example.com/video.mp4" 
                                                       range:range];
player.delegate = self;
[player prepare];
```

### Network Streaming Media Playback

```objc
// HLS stream playback
SlarkPlayer *hlsPlayer = [[SlarkPlayer alloc] init:@"https://example.com/stream.m3u8"];
hlsPlayer.delegate = self;

[hlsPlayer prepare];
[hlsPlayer play];
```

### Lifecycle Management

```objc
@interface PlayerViewController ()
@property (nonatomic, strong) SlarkPlayer *player;
@end

@implementation PlayerViewController

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    // Resume playback if needed
}

- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    // Pause playback when view disappears
    [self.player pause];
}

- (void)dealloc {
    // Clean up resources
    [self.player stop];
    self.player.delegate = nil;
    self.player = nil;
}

@end
```

## Important Notes

1. **Thread Safety**: All observer callbacks are executed on background threads, UI operations must be dispatched to the main queue
2. **Memory Management**: Use weak references when setting delegates to avoid retain cycles
3. **Prepare Before Play**: Must call `prepare` method before performing playback operations
4. **View Lifecycle**: Call `stop` method at appropriate times to release resources
5. **Network Permissions**: Network playback requires configuring network access permissions in Info.plist
6. **CMTime Usage**: iOS uses CMTime for precise time representation, which provides better accuracy than double values

## Error Handling

When errors occur during playback, they are notified through the following ways:

1. Player state changes to `SlarkPlayerStateError`
2. Triggers `SlarkPlayerEventOnError` event
3. Error information is passed through the observer protocol's `notifyEvent` method

```objc
- (void)notifyEvent:(NSString*)playerId event:(SlarkPlayerEvent)event value:(NSString*)value {
    if (event == SlarkPlayerEventOnError) {
        NSInteger errorCode = [value integerValue];
        switch (errorCode) {
            case SlarkPlayerErrorNetWorkError:
                NSLog(@"Network error occurred");
                break;
            case SlarkPlayerErrorFileError:
                NSLog(@"File error occurred");
                break;
            case SlarkPlayerErrorDecodeError:
                NSLog(@"Decode error occurred");
                break;
            default:
                NSLog(@"Unknown error: %@", value);
                break;
        }
    }
}
```

## Network Configuration

For network playback, add the following to your app's `Info.plist`:

```xml
<key>NSAppTransportSecurity</key>
<dict>
    <key>NSAllowsArbitraryLoads</key>
    <true/>
</dict>
```

Note: For production apps, consider using more specific security configurations instead of allowing arbitrary loads.