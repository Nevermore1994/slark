# Slark

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-iOS%20%7C%20Android-lightgrey.svg)
![Version](https://img.shields.io/badge/version-0.0.1-green.svg)

- [中文文档](README_zh.md)

Slark is a cross-platform video player that supports iOS and Android platforms. Built from the ground up with modern C++23, it provides a streamlined media playback solution with minimal dependencies (only OpenSSL for HTTPS support).

## 🔧 Supported Platforms

### iOS
- **Minimum Version**: iOS 16.5+
- **Architecture**: arm64
- **Integration**: CocoaPods

### Android  
- **Minimum Version**: API Level 29 (Android 10.0)+
- **Architecture**: arm64-v8a
- **Integration**: Gradle/CMake

## 📦 Installation

### iOS Integration

Add to your `Podfile`:

```ruby
pod 'slark', '~> 0.0.1'
```

Then run:
```bash
pod install
```

### Android Integration

Add dependency to your `build.gradle`:

```gradle
dependencies {
    implementation 'com.slark:sdk:0.0.1'
}
```

## 🚀 Quick Start

### iOS Usage Example

```objc
#import <SlarkPlayer.h>

// Create player
SlarkPlayer *player = [[SlarkPlayer alloc] init:@"path/to/video.mp4"];
player.delegate = self; // Set observer
self.playerController = [[SlarkViewController alloc] initWithPlayer:self.view.bounds player:player];
[self addChildViewController:self.playerController]; //add ViewController

//To add the player view to your view
[self.view addSubview:self.playerController.view];
//


// Set observer
player.delegate = self;

// Playback controls
[player prepare];
[player play];
[player pause];
[player stop];

// Playback controls
[player seek:30.0];  // Seek to 30 seconds
[player setVolume:80.0f];  // Set volume
[player setLoop:YES];  // Enable loop playback

// Implement observer protocol
- (void)notifyTime:(NSString*)playerId time:(double)time {
    // Playback progress callback
}

- (void)notifyState:(NSString*)playerId state:(SlarkPlayerState)state {
    // Playback state change
}

- (void)notifyEvent:(NSString*)playerId event:(SlarkPlayerEvent)event value:(NSString*)value {
    // Playback event callback
}
```

### Android Usage Example

```kotlin
import com.slark.api.*

// Initialize SDK
SlarkSdk.init(application)

// Create player configuration
val config = SlarkPlayerConfig("path/to/video.mp4")
val player = SlarkPlayerFactory.createPlayer(config)
//Setting the render target
player?.setRenderTarget(SlarkRenderTarget.FromTextureView(textureView))

// Playback controls
player?.prepare()
player?.play()
player?.pause()
player?.stop()

// Set observer
player?.setObserver(object : SlarkPlayerObserver {
    override fun onTimeUpdate(playerId: String, time: Double) {
        // Playback progress callback
    }
    
    override fun onStateChanged(playerId: String, state: SlarkPlayerState) {
        // Playback state change
    }
    
    override fun onEvent(playerId: String, event: SlarkPlayerEvent, value: String) {
        // Playback event callback
    }
})

// Playback controls
player?.seek(30.0)  // Seek to 30 seconds
player?.volume = 80  // Set volume
player?.isLoop = true  // Enable loop playback
```

## 🔧 Development & Build

### Requirements

- **macOS**: Xcode 14.0+
- **CMake**: 3.20+
- **C++**: C++23 support
- **iOS**: iOS 16.5+ SDK
- **Android**: NDK r21+, API Level 29+

### Build Steps

1. **Clone Repository**:
```bash
git clone https://github.com/Nevermore1994/slark.git
cd slark
```

2. **iOS Build**:
```bash
cd script
python build.py -p iOS
```

3. **Android Build**:
```bash
cd script
python build.py -p Android
```

## 📖 Demo Projects

The project includes complete example code:

- **iOS Demo**: `demo/iOS/demo/` - Demonstrates audio and video playback
- **Android Demo**: `demo/Android/` - Demonstrates local and network playback

Run demos:

```bash
# iOS
cd script && python build.py -p iOS --demo

# Android  
cd script && python build.py -p Android --demo
```

## 📚 API Documentation

For detailed API documentation, please refer to:
- [iOS API Documentation](docs/ios-api.md)
- [Android API Documentation](docs/android-api.md)

## 🗺️ Roadmap

- [ ] Startup time optimization
- [ ] Performance optimization
- [ ] Support FLV & DASH
- [ ] Support H265 & AV1
- [ ] HDR content support 
- [ ] Variable playback speed (0.5x - 2.0x)
- [ ] Support live streaming 


## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 👨‍💻 Author

- **Nevermore** - [875229565@qq.com](mailto:875229565@qq.com)

⭐ If this project helps you, please give it a Star!
