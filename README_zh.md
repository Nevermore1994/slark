# Slark

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-iOS%20%7C%20Android-lightgrey.svg)
![Version](https://img.shields.io/badge/version-0.0.1-green.svg)
![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)

[![Build Status](https://github.com/Nevermore1994/slark/workflows/Slark%20Build/badge.svg)](https://github.com/Nevermore1994/slark/actions)
[![macOS Build](https://img.shields.io/github/actions/workflow/status/Nevermore1994/slark/cmake.yml?label=macOS&branch=main)](https://github.com/Nevermore1994/slark/actions)
[![iOS Build](https://img.shields.io/github/actions/workflow/status/Nevermore1994/slark/cmake.yml?label=iOS&branch=main)](https://github.com/Nevermore1994/slark/actions)
[![Android Build](https://img.shields.io/github/actions/workflow/status/Nevermore1994/slark/cmake.yml?label=Android&branch=main)](https://github.com/Nevermore1994/slark/actions)

- [English Documentation](README.md)

Slark 是一款跨平台视频播放器，支持 iOS 和 Android 平台。采用现代 C++23 从零构建，提供精简的媒体播放解决方案，具有极少的外部依赖。

## 🔧 支持平台

### iOS
- **最低版本**：iOS 16.5+
- **架构**：arm64（真机），x86_64/arm64（模拟器）
- **集成方式**：CocoaPods

### Android  
- **最低版本**：API Level 29 (Android 10.0)+
- **架构**：arm64-v8a, armeabi-v7a, x86_64
- **集成方式**：Gradle/CMake

## 🔒 OpenSSL 依赖

Slark 使用 OpenSSL 3.0.13 提供 HTTPS 支持。**OpenSSL 从源码编译**，以确保：
- ✅ 安全性：完全控制编译选项
- ✅ 一致性：所有平台使用相同版本
- ✅ 交叉编译：原生支持 iOS 和 Android

### 构建行为

- **源码缓存**：OpenSSL 源码缓存在 `.cache/openssl/` 目录，避免重复下载
- **构建缓存**：编译产物缓存在 `build/deps/openssl/` 目录
- **自动下载**：从 OpenSSL 官方发布源自动获取

### 禁用 HTTPS

如果不需要 HTTPS 支持，可以禁用：

```bash
cmake -B build -DDISABLE_HTTPS=ON
```

或使用构建脚本：

```bash
./build.sh --disable_http
```

## 📦 安装

### iOS 集成

添加到你的 `Podfile`：

```ruby
pod 'slark', '~> 0.0.1'
```

然后运行：
```bash
pod install
```

### Android 集成

在你的 `build.gradle` 中添加依赖：

```gradle
dependencies {
    implementation 'com.slark:sdk:0.0.1'
}
```

## 🚀 快速开始

### iOS 使用示例

```objc
#import <SlarkPlayer.h>

// 创建播放器
SlarkPlayer *player = [[SlarkPlayer alloc] init:@"path/to/video.mp4"];
player.delegate = self; // 设置观察者
self.playerController = [[SlarkViewController alloc] initWithPlayer:self.view.bounds player:player];
[self addChildViewController:self.playerController]; // 添加视图控制器

// 将播放器视图添加到你的视图中
[self.view addSubview:self.playerController.view];

// 设置观察者
player.delegate = self;

// 播放控制
[player prepare];
[player play];
[player pause];
[player stop];

// 播放控制
[player seek:30.0];  // 跳转到 30 秒
[player setVolume:0.8f];  // 设置音量
[player setLoop:YES];  // 启用循环播放

// 实现观察者协议
- (void)notifyTime:(NSString*)playerId time:(double)time {
    // 播放进度回调
}

- (void)notifyState:(NSString*)playerId state:(SlarkPlayerState)state {
    // 播放状态变化
}

- (void)notifyEvent:(NSString*)playerId event:(SlarkPlayerEvent)event value:(NSString*)value {
    // 播放事件回调
}
```

### Android 使用示例

```kotlin
import com.slark.api.*

// 初始化 SDK
SlarkSdk.init(application)

// 创建播放器配置
val config = SlarkPlayerConfig("path/to/video.mp4")
val player = SlarkPlayerFactory.createPlayer(config)
// 设置渲染目标
player?.setRenderTarget(SlarkRenderTarget.FromTextureView(textureView))

// 播放控制
player?.prepare()
player?.play()
player?.pause()
player?.stop()

// 设置观察者
player?.setObserver(object : SlarkPlayerObserver {
    override fun onTimeUpdate(playerId: String, time: Double) {
        // 播放进度回调
    }
    
    override fun onStateChanged(playerId: String, state: SlarkPlayerState) {
        // 播放状态变化
    }
    
    override fun onEvent(playerId: String, event: SlarkPlayerEvent, value: String) {
        // 播放事件回调
    }
})

// 播放控制
player?.seek(30.0)  // 跳转到 30 秒
player?.volume = 80  // 设置音量
player?.isLoop = true  // 启用循环播放
```

## 🔧 开发与构建

### 环境要求

- **macOS**：Xcode 14.0+
- **CMake**：3.20+
- **C++**：支持 C++23
- **iOS**：iOS 16.5+ SDK
- **Android**：NDK r21+，API Level 29+

### 构建步骤

1. **克隆仓库**：
```bash
git clone https://github.com/Nevermore1994/slark.git
cd slark
```

2. **iOS 构建**：
```bash
./build.sh -p iOS
```

3. **Android 构建**：
```bash
./build.sh -p Android
```

## 📖 示例项目

项目包含完整的示例代码：

- **iOS 示例**：`demo/iOS/demo/` - 演示音频和视频播放
- **Android 示例**：`demo/Android/` - 演示本地和网络播放

运行示例：

```bash
# iOS
./build.sh -p iOS --demo

# Android  
./build.sh -p Android --demo
```

## 📚 API 文档

详细的 API 文档请参考：
- [iOS API 文档](docs/ios-api.md)
- [Android API 文档](docs/android-api.md)

## 🗺️ 路线图

- [ ] 启动时间优化
- [ ] 性能优化
- [ ] 支持 FLV 和 DASH
- [ ] 支持 H265 和 AV1
- [ ] HDR 内容支持
- [ ] 变速播放 (0.5x - 2.0x)
- [ ] 支持直播流

## 📄 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件。

## 👨‍💻 作者

- **Nevermore** - [875229565@qq.com](mailto:875229565@qq.com)

⭐ 如果这个项目对你有帮助，请给它一个 Star！ 