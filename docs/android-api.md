
## Android API 文档

# Slark Android API Documentation

Slark Android SDK provides complete media playback functionality, supporting both local file and network streaming media playback.

## Table of Contents

- [Core Classes](#core-classes)
  - [SlarkPlayer](#slarkplayer)
  - [SlarkPlayerFactory](#slarkplayerfactory)
  - [SlarkPlayerConfig](#slarkplayerconfig)
- [Interfaces](#interfaces)
  - [SlarkPlayerObserver](#slarkplayerobserver)
- [Enumerations](#enumerations)
  - [SlarkPlayerState](#slarkplayerstate)
  - [SlarkPlayerEvent](#slarkplayerevent)
  - [PlayerErrorCode](#playererrorcode)
- [Render Target](#render-target)
  - [SlarkRenderTarget](#slarkrendertarget)
- [Time Classes](#time-classes)
  - [KtTime](#kttime)
  - [KtTimeRange](#kttimerange)
- [Usage Examples](#usage-examples)

## Core Classes

### SlarkPlayer

Main media player interface that provides complete audio and video playback control functionality.

#### Properties

```kotlin
var isLoop: Boolean
```
- **Description**: Loop playback mode
- **Range**: true/false

```kotlin
var isMute: Boolean
```
- **Description**: Mute state
- **Range**: true/false

```kotlin
var volume: Float
```
- **Description**: Volume control
- **Range**: 0.0 - 100.0

#### Playback Control Methods

```kotlin
fun prepare()
```
- **Description**: Prepare for playback, must be called before playing
- **Note**: Asynchronous operation, status changes will be notified through observer upon completion

```kotlin
fun play()
```
- **Description**: Start playing media

```kotlin
fun pause()
```
- **Description**: Pause playback

```kotlin
fun release()
```
- **Description**: Stop playback and release media resources

#### Playback Position Control

```kotlin
fun seekTo(time: Double, isAccurate: Boolean)
```
- **Description**: Fast forward/rewind to specified time position
- **Parameters**: 
  - `time`: Target time position (seconds)
  - `isAccurate`: Whether to use accurate positioning (recommended when user releases progress bar)

#### Render Control

```kotlin
fun setRenderTarget(renderTarget: SlarkRenderTarget)
```
- **Description**: Set video render target
- **Parameters**: 
  - `renderTarget`: Render target object

```kotlin
fun setRenderSize(width: Int, height: Int)
```
- **Description**: Set render size
- **Parameters**: 
  - `width`: Render width
  - `height`: Render height

```kotlin
fun setRotation(rotation: Rotation)
```
- **Description**: Set video rotation angle
- **Parameters**: 
  - `rotation`: Rotation angle enumeration value

#### Observer Management

```kotlin
fun setObserver(observer: SlarkPlayerObserver?)
```
- **Description**: Set player event observer
- **Parameters**: 
  - `observer`: Observer instance, pass null to cancel observation

#### Information Retrieval Methods

```kotlin
fun playerId(): String
```
- **Description**: Get player unique identifier
- **Return Value**: Player ID string

```kotlin
fun state(): SlarkPlayerState
```
- **Description**: Get current player state
- **Return Value**: Player state enumeration value

```kotlin
fun totalDuration(): Double
```
- **Description**: Get total media duration
- **Return Value**: Total duration (seconds)

```kotlin
fun currentTime(): Double
```
- **Description**: Get current playback time
- **Return Value**: Current playback time (seconds)

#### Lifecycle Management

```kotlin
fun onBackground(isBackground: Boolean)
```
- **Description**: Handle application background state changes
- **Parameters**: 
  - `isBackground`: true means entering background, false means returning to foreground

#### Rotation Enumeration

```kotlin
enum class Rotation {
    ROTATION_0,   // 0 degrees
    ROTATION_90,  // 90 degrees
    ROTATION_180, // 180 degrees
    ROTATION_270  // 270 degrees
}
```

### SlarkPlayerFactory

Player factory class for creating player instances.

#### Methods

```kotlin
fun createPlayer(config: SlarkPlayerConfig): SlarkPlayer?
```
- **Description**: Create player instance based on configuration
- **Parameters**: 
  - `config`: Player configuration object
- **Return Value**: Player instance, returns null if creation fails

### SlarkPlayerConfig

Player configuration class.

```kotlin
data class SlarkPlayerConfig(
    val dataSource: String, // URL or file path
    val timeRange: KtTimeRange = KtTimeRange.zero // Playback time range
)
```

#### Parameter Description

- **dataSource**: Media data source, supports:
  - Local file path: `/sdcard/video.mp4`
  - HTTP/HTTPS URL: `https://example.com/video.mp4`
  - HLS stream: `https://example.com/stream.m3u8`
- **timeRange**: Playback time range, defaults to entire file

## Interfaces

### SlarkPlayerObserver

Player observer interface for receiving playback status and event notifications.

#### Methods

```kotlin
fun notifyTime(playerId: String, time: Double)
```
- **Description**: Playback time update callback
- **Parameters**: 
  - `playerId`: Player ID
  - `time`: Current playback time (seconds)
- **Call Frequency**: Called approximately every 100ms

```kotlin
fun notifyState(playerId: String, state: SlarkPlayerState)
```
- **Description**: Playback state change callback
- **Parameters**: 
  - `playerId`: Player ID
  - `state`: New playback state

```kotlin
fun notifyEvent(playerId: String, event: SlarkPlayerEvent, value: String)
```
- **Description**: Playback event callback
- **Parameters**: 
  - `playerId`: Player ID
  - `event`: Event type
  - `value`: Event-related value

## Enumerations

### SlarkPlayerState

Player state enumeration.

```kotlin
enum class SlarkPlayerState {
    Unknown,        // Unknown state
    Initializing,   // Initializing
    Prepared,       // Prepared
    Buffering,      // Buffering
    Ready,          // Ready to play
    Playing,        // Playing
    Pause,          // Paused
    Stop,           // Stopped
    Error,          // Error
    Completed,      // Playback completed
}
```

### SlarkPlayerEvent

Player event enumeration.

```kotlin
enum class SlarkPlayerEvent {
    FirstFrameRendered,  // First frame rendered
    SeekDone,           // Seek completed
    PlayEnd,            // Playback ended
    UpdateCacheTime,    // Cache updated
    OnError,            // Error occurred
}
```

### PlayerErrorCode

Player error code enumeration.

```kotlin
enum class PlayerErrorCode {
    OpenFileError,  // File open error
    NetWorkError,   // Network error
    NotSupport,     // Format not supported
    DemuxError,     // Demux error
    DecodeError,    // Decode error
    RenderError;    // Render error
    
    companion object {
        fun fromString(value: String): PlayerErrorCode? {
            return when (value.toInt()) {
                0 -> OpenFileError
                1 -> NetWorkError
                2 -> NotSupport
                3 -> DemuxError 
                4 -> DecodeError
                5 -> RenderError
                else -> null
            }
        }
    }
}
```

## Render Target

### SlarkRenderTarget

Sealed class for video render targets, supporting multiple rendering methods.

```kotlin
sealed class SlarkRenderTarget {
    data class FromSurfaceView(val view: SurfaceView) : SlarkRenderTarget()
    data class FromTextureView(val view: TextureView) : SlarkRenderTarget()
    data class FromSurface(val surface: Surface, val width: Int, val height: Int) : SlarkRenderTarget()
}
```

#### Render Target Types

1. **FromSurfaceView**: Render using SurfaceView
   - Suitable for fullscreen playback
   - Best performance

2. **FromTextureView**: Render using TextureView
   - Supports animations and transformations
   - Can work with other UI components

3. **FromSurface**: Render using native Surface
   - Maximum flexibility
   - Requires manual size management

## Time Classes

### KtTime

Time representation class with precision control.

```kotlin
data class KtTime(val value: Long, val timescale: Int = 1000) : Comparable<KtTime> {
    fun toSeconds(): Double
    
    operator fun plus(other: KtTime): KtTime
    operator fun minus(other: KtTime): KtTime
    
    fun isValid(): Boolean
    
    companion object {
        fun fromSeconds(seconds: Double, timescale: Int = 600): KtTime
        val zero: KtTime
    }
}
```

#### Properties and Methods

- **value**: Time value in the specified timescale units
- **timescale**: Number of units per second (default: 1000 for milliseconds)
- **toSeconds()**: Convert to seconds as Double
- **fromSeconds()**: Create KtTime from seconds
- **Arithmetic operators**: Support addition and subtraction
- **Comparison**: Implements Comparable interface

### KtTimeRange

Time range class for specifying playback time segments.

```kotlin
data class KtTimeRange(val start: KtTime, val duration: KtTime) {
    val end: KtTime
    
    fun get(): Pair<Double, Double>
    fun contains(time: KtTime): Boolean
    fun isOverlap(other: KtTimeRange): Boolean
    fun overlap(other: KtTimeRange): KtTimeRange?
    fun union(other: KtTimeRange): KtTimeRange?
    fun isValid(): Boolean
    
    companion object {
        val zero: KtTimeRange
    }
}
```

#### Properties and Methods

- **start**: Start time of the range
- **duration**: Duration of the range
- **end**: Computed end time (start + duration)
- **get()**: Returns start and end times as seconds pair
- **contains()**: Check if a time is within the range
- **isOverlap()**: Check if two ranges overlap
- **overlap()**: Get overlapping portion of two ranges
- **union()**: Combine two ranges into one
- **isValid()**: Check if range is valid

## Usage Examples

### Basic Playback Flow

```kotlin
class PlayerActivity : AppCompatActivity(), SlarkPlayerObserver {
    private var player: SlarkPlayer? = null
    private lateinit var textureView: TextureView
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_player)
        
        // 1. Create player configuration
        val config = SlarkPlayerConfig("https://example.com/video.mp4")
        
        // 2. Create player
        player = SlarkPlayerFactory.createPlayer(config)
        
        // 3. Set observer
        player?.setObserver(this)
        
        // 4. Set render target
        textureView = findViewById(R.id.texture_view)
        player?.setRenderTarget(SlarkRenderTarget.FromTextureView(textureView))
        
        // 5. Prepare for playback
        player?.prepare()
    }
    
    // Playback control
    private fun playVideo() {
        player?.play()
    }
    
    private fun pauseVideo() {
        player?.pause()
    }
    
    private fun seekToTime(time: Double) {
        player?.seekTo(time, false)
    }
    
    private fun setVolume(volume: Float) {
        player?.volume = volume
    }
    
    override fun onDestroy() {
        super.onDestroy()
        player?.release()
    }
    
    // SlarkPlayerObserver implementation
    override fun notifyTime(playerId: String, time: Double) {
        // Update playback progress UI
        runOnUiThread {
            Log.d("Player", "Playback time: $time")
            // Update progress bar
        }
    }
    
    override fun notifyState(playerId: String, state: SlarkPlayerState) {
        runOnUiThread {
            when (state) {
                SlarkPlayerState.Playing -> {
                    Log.d("Player", "Playback started")
                    // Update play button state
                }
                SlarkPlayerState.Pause -> {
                    Log.d("Player", "Playback paused")
                }
                SlarkPlayerState.Completed -> {
                    Log.d("Player", "Playback completed")
                }
                SlarkPlayerState.Error -> {
                    Log.e("Player", "Playback error")
                }
                else -> {}
            }
        }
    }
    
    override fun notifyEvent(playerId: String, event: SlarkPlayerEvent, value: String) {
        runOnUiThread {
            when (event) {
                SlarkPlayerEvent.FirstFrameRendered -> {
                    Log.d("Player", "First frame rendered")
                }
                SlarkPlayerEvent.SeekDone -> {
                    Log.d("Player", "Seek completed")
                }
                SlarkPlayerEvent.OnError -> {
                    Log.e("Player", "Playback error: $value")
                    // Handle error
                }
                else -> {}
            }
        }
    }
}
```

### Audio Playback Example

```kotlin
class AudioPlayerActivity : AppCompatActivity(), SlarkPlayerObserver {
    private var audioPlayer: SlarkPlayer? = null
    
    private fun setupAudioPlayer() {
        // Create audio player
        val config = SlarkPlayerConfig("/sdcard/audio.wav")
        audioPlayer = SlarkPlayerFactory.createPlayer(config)
        
        // Set audio properties
        audioPlayer?.apply {
            setObserver(this@AudioPlayerActivity)
            volume = 80.0f
            isLoop = true
        }
        
        // Audio playback doesn't need render target
        audioPlayer?.prepare()
        audioPlayer?.play()
    }
}
```

### Time Range Playback Example

```kotlin
// Play specific time range
val startTime = KtTime.fromSeconds(30.0)  // Start at 30 seconds
val duration = KtTime.fromSeconds(60.0)   // Play for 60 seconds
val timeRange = KtTimeRange(startTime, duration)

val config = SlarkPlayerConfig(
    dataSource = "https://example.com/video.mp4",
    timeRange = timeRange
)

val player = SlarkPlayerFactory.createPlayer(config)
```

### Different Render Target Examples

```kotlin
// Using SurfaceView
val surfaceView: SurfaceView = findViewById(R.id.surface_view)
player?.setRenderTarget(SlarkRenderTarget.FromSurfaceView(surfaceView))

// Using TextureView
val textureView: TextureView = findViewById(R.id.texture_view)
player?.setRenderTarget(SlarkRenderTarget.FromTextureView(textureView))

// Using native Surface
val surface = Surface(surfaceTexture)
player?.setRenderTarget(SlarkRenderTarget.FromSurface(surface, 1920, 1080))
```

### Lifecycle Management

```kotlin
class PlayerActivity : AppCompatActivity() {
    private var player: SlarkPlayer? = null
    
    override fun onResume() {
        super.onResume()
        player?.onBackground(false)
    }
    
    override fun onPause() {
        super.onPause()
        player?.onBackground(true)
    }
    
    override fun onDestroy() {
        super.onDestroy()
        player?.release()
        player = null
    }
}
```

### Error Handling

```kotlin
override fun notifyEvent(playerId: String, event: SlarkPlayerEvent, value: String) {
    when (event) {
        SlarkPlayerEvent.OnError -> {
            val errorCode = PlayerErrorCode.fromString(value)
            when (errorCode) {
                PlayerErrorCode.NetWorkError -> {
                    // Handle network error
                    showErrorDialog("Network connection failed, please check network settings")
                }
                PlayerErrorCode.OpenFileError -> {
                    // Handle file open error
                    showErrorDialog("File cannot be opened or does not exist")
                }
                PlayerErrorCode.DecodeError -> {
                    // Handle decode error
                    showErrorDialog("Video format not supported")
                }
                else -> {
                    showErrorDialog("Playback error: $value")
                }
            }
        }
        else -> {}
    }
}
```

## Important Notes

1. **SDK Initialization**: Must call `SlarkSdk.init(application)` before use
2. **Thread Safety**: Observer callbacks may execute on background threads, UI operations need to switch to main thread
3. **Resource Management**: Call `release()` method in time to release player resources
4. **Network Permissions**: Network playback requires adding network permissions in AndroidManifest.xml
5. **Lifecycle**: Properly handle application lifecycle to avoid memory leaks
6. **Time Precision**: Use KtTime for precise time calculations and KtTimeRange for time segments

## Permission Configuration

Add necessary permissions in `AndroidManifest.xml`:

```xml
<!-- Network permissions -->
<uses-permission android:name="android.permission.INTERNET" />
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />

<!-- Storage permissions (for playing local files) -->
<uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" />
```

## ProGuard Configuration

If using code obfuscation, add to `proguard-rules.pro`:

```
# Slark SDK
-keep class com.slark.** { *; }
-dontwarn com.slark.**
```