## codec 架构介绍

### 文档目标

本文档用于介绍 Slark 播放内核中 `src/core/codec` 模块的架构组成、职责边界与运行流程。内容以“现状说明”为主，不包含设计优劣评判。

### 模块在播放器中的位置

`codec` 位于播放器中后段，承接 demux 输出包并产出可渲染帧。

- 上游：`PacketHandler`、音视频包队列（`AVFrameType::Audio/Video`）
- 中间：`DecoderComponent` + `IDecoder` 具体实现
- 下游：`AudioRenderComponent`、`IVideoRender`

---

## 分层与核心对象

### 1) 解码抽象层：`IDecoder`

文件：`src/core/codec/IDecoder.hpp`

`IDecoder` 定义统一解码契约，核心能力包括：

- `open(std::shared_ptr<DecoderConfig>)`
- `decode(AVFrameRefPtr&)`
- `flush()` / `reset()` / `close()`
- `setReceiveFunc(DecoderReceiveFunc)`

关键状态：

- `isOpen_`：解码器是否可用
- `isCompleted_`：是否完成（通常由 EOS 触发）
- `decoderType_`：当前解码器类型

分类方法：

- `isAudioDecoder(DecoderType)`
- `isVideoDecoder(DecoderType)`

`DecoderErrorCode` 统一了解码调用结果语义（`Success`、`Again`、`InputDataError` 等）。

---

### 2) 类型解析与工厂层：`DecoderManager`

文件：`src/core/codec/DecoderManager.hpp`、`src/core/codec/DecoderManager.cc`

`DecoderManager` 是解码器类型选择和实例创建入口：

- `getDecoderType(mediaInfo, isSoftDecode)`：根据媒体 mime 与软/硬解标志映射到 `DecoderType`
- `create(DecoderType)`：基于反射创建具体 `IDecoder`
- `contains(DecoderType)`：判断当前平台是否提供该类型

内部通过两张映射表组织：

- `mediaInfo_`：`mediaInfo(+SW/HW)` → `DecoderType`
- `decoderInfo_`：`DecoderType` → `DecoderTypeInfo`

平台条件编译（`SLARK_IOS` / `SLARK_ANDROID`）决定硬解条目注册。

---

### 3) 运行时编排层：`DecoderComponent`

文件：`src/core/codec/DecoderComponent.hpp`、`src/core/codec/DecoderComponent.cc`

`DecoderComponent` 负责把 `IDecoder` 接入播放器运行时，主要职责：

- 管理输入队列：`pendingDecodeQueue_`
- 管理解码线程：`decodeWorker_`
- 生命周期控制：`open/start/pause/flush/close`
- EOS 推送与完成态传播
- 通过 `DecoderReceiveFunc` 把解码后帧回调给上层

关键点：

- `open()` 异步创建并打开解码器，避免阻塞调用线程。
- `send()` 入队并唤醒工作线程。
- `pushFrameDecode()` 按周期执行实际解码，支持 `isFastPush` 连续送帧。
- 输入结束后，构造 EOS 帧（`buildEOSFrame()`）推进解码器完成流程。

---

## 配置模型

文件：`src/core/codec/DecoderConfig.hpp`

### 基类：`DecoderConfig`

通用字段：

- `playerId`
- `timeScale`
- `mediaInfo`
- `profile`
- `level`

### 音频配置：`AudioDecoderConfig`

从 `AudioInfo` 初始化：

- 声道数、位深、采样率
- AAC 相关索引与扩展字段

### 视频配置：`VideoDecoderConfig`

从 `VideoInfo` 初始化：

- `naluHeaderLength`
- 分辨率
- `sps/pps/vps`
- profile/level/timescale

---

## 具体解码器实现（当前可见）

### `RawDecoder`

文件：`src/core/codec/RawDecoder.hpp`、`src/core/codec/RawDecoder.cc`

`RawDecoder` 是透传型解码器：

- `open()` 保存配置并置 `isOpen_`
- `decode()`
  - 普通帧：直接 `invokeReceiveFunc(std::move(frame))`
  - EOS 帧：置 `isCompleted_ = true`
- `flush()` 重置完成态
- `close()` 清理配置与状态

该实现用于 raw 音频路径（如 PCM）。

---

## 从播放器到 codec 的调用链

1. demux 产出音视频包后进入上层包处理。
2. `PipelineScheduler` 将待解码包推入 `DecoderComponent::send()`。
3. `DecoderComponent` 工作线程周期执行 `pushFrameDecode()`。
4. 具体 `IDecoder::decode()` 输出解码帧。
5. 输出帧通过 `DecoderReceiveFunc` 回到播放器后续渲染链路。

可表示为：

```text
Demux output packet
  -> DecoderComponent::send
  -> IDecoder::decode
  -> DecoderReceiveFunc callback
  -> AudioRender / VideoRender
```

---

## 生命周期与状态协作

### `DecoderComponent` 侧

- `open(type, config)`：异步构建底层 decoder
- `start()`：有输入时启动工作线程
- `pause()`：暂停工作线程
- `flush()`：清空待解码队列并 `decoder->flush()`
- `setInputCompleted()`：标记输入结束，触发 EOS 注入
- `close()`：关闭并释放底层 decoder

### `IDecoder` 侧

- `open()`：建立解码上下文
- `decode()`：处理输入帧或 EOS 帧
- `flush()/reset()`：清理内部状态
- `close()`：关闭资源

---

## 目录结构（codec）

```text
src/core/codec/
├── IDecoder.hpp
├── DecoderManager.hpp
├── DecoderManager.cc
├── DecoderConfig.hpp
├── DecoderComponent.hpp
├── DecoderComponent.cc
├── RawDecoder.hpp
└── RawDecoder.cc
```

---

## 图形抽象层

### C++ 侧接口

文件：`include/IGraphicsContext.h`

为支持多后端（OpenGL/Vulkan），定义了图形资源抽象接口：

- `ITexture`：纹理抽象（width/height/isValid/bind/unbind）
- `IFrameBuffer`：帧缓冲抽象（bind/unbind/isValid/texture）
- `IGraphicsSurface`：图形表面抽象（nativeSurface/isValid）
- `IGraphicsContext`：图形上下文抽象（backend/init/release/attachContext/detachContext/createOffscreenSurface/acquireFrameBuffer/createTexture）

后端类型通过 `GraphicsBackendType` 枚举区分（OpenGL/Vulkan）。

### OpenGL 后端实现

| 类                       | 文件                               | 职责                                                                  |
| ------------------------ | ---------------------------------- | --------------------------------------------------------------------- |
| `OpenGLEGraphicsContext` | `cpp/OpenGLEGraphicsContext.h/.cc` | 封装 `AndroidEGLContext` + `FrameBufferPool`，实现 `IGraphicsContext` |
| `OpenGLTexture`          | `cpp/OpenGLTexture.h`              | 适配 `Texture` 为 `ITexture`                                          |
| `OpenGLFrameBuffer`      | `cpp/OpenGLFrameBuffer.h`          | 适配 `FrameBuffer` 为 `IFrameBuffer`                                  |
| `OpenGLSurface`          | `cpp/OpenGLSurface.h`              | 封装 `EGLSurface` 为 `IGraphicsSurface`                               |

### Kotlin 侧接口

| 类                                      | 职责                              |
| --------------------------------------- | --------------------------------- |
| `IGraphicsContext`                      | Kotlin 侧图形上下文接口           |
| `IGraphicsSurface`                      | Kotlin 侧图形表面接口             |
| `ITexture` / `IFrameBuffer`             | Kotlin 侧纹理/帧缓冲接口          |
| `ITextureRenderer` / `IPreviewRenderer` | Kotlin 侧渲染器接口               |
| `OpenGLContext`                         | `IGraphicsContext` 的 OpenGL 实现 |
| `GLTextureRenderer`                     | `ITextureRenderer` 的 OpenGL 实现 |
| `GLPreviewRenderer`                     | `IPreviewRenderer` 的 OpenGL 实现 |
| `IRenderThread` / `GLRenderThread`      | 渲染线程抽象与 OpenGL 实现        |

### VideoDecodeResource 重构

`VideoDecodeResource` 从直接持有 `AndroidEGLContext` + `FrameBufferPool` 改为持有 `IGraphicsContextRefPtr`，通过抽象接口获取表面和帧缓冲资源。

### VideoHardwareDecoder 重构

`VideoHardwareDecoder` 从直接使用 `EGLSurface` 改为使用 `IGraphicsSurfaceRefPtr`，通过 `IGraphicsContext` 工厂方法获取资源。

### GLContextManager 扩展

新增 `createGraphicsContextWithId(id)` 方法，返回 `IGraphicsContextRefPtr`，保持旧 `createShareContextWithId(id)` 向后兼容。

---

## 一句话总结

当前 `codec` 架构可概括为：`DecoderComponent` 负责运行时调度，`IDecoder` 统一解码契约，`DecoderManager` 负责类型解析与实例创建，具体解码器实现负责媒介格式解码并通过回调输出帧到渲染链路。图形资源通过 `IGraphicsContext` 抽象层管理，支持多后端（OpenGL/Vulkan）扩展。