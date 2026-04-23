## PlayerImpl 架构介绍

### 文档目标

本文档介绍 Slark 播放内核中 `Player::Impl` 与 `PlayPipeline` 的架构组成、职责边界与运行流程。

---

## 模块定位

`Player::Impl` 是播放器核心**编排层**，负责状态机、事件循环、生命周期管理与外部通知。

`PlayPipeline` 是播放器核心**管线层**，持有数据链路的所有组件和队列，对外暴露生命周期接口和事件回调。

整体链路：

```
外部控制命令（play/pause/seek/stop/setting）
        ↓
  Player::Impl（状态机 + 事件循环 + 通知中心）
        ↓ build/start/pause/seek/teardown
  PlayPipeline（管线协调器）
        ↓
  IStream → DemuxerComponent → DecoderComponent(Audio/Video)
                                    ↓
                         AudioRenderComponent / IVideoRender
```

---

## 核心文件

- [PlayerImpl.h](/Users/nevermore-/selfspace/slark/src/core/PlayerImpl.h)
- [PlayerImpl.cpp](/Users/nevermore-/selfspace/slark/src/core/PlayerImpl.cpp)
- [PlayPipeline.h](/Users/nevermore-/selfspace/slark/src/core/pipeline/PlayPipeline.h)
- [PlayPipeline.cpp](/Users/nevermore-/selfspace/slark/src/core/pipeline/PlayPipeline.cpp)
- [SeekCoordinator.h](/Users/nevermore-/selfspace/slark/src/core/pipeline/SeekCoordinator.h)
- [ClockSync.h](/Users/nevermore-/selfspace/slark/src/core/pipeline/ClockSync.h)
- [PipelineEvent.h](/Users/nevermore-/selfspace/slark/src/core/pipeline/PipelineEvent.h)
- [PipelineConfig.h](/Users/nevermore-/selfspace/slark/src/core/pipeline/PipelineConfig.h)

---

## Player::Impl

### 主要成员

- **状态与参数**：`state_`、`params_`、`info_`、`bufferingReason_`
- **线程与事件**：`ownerThread_`、`sender_/receiver_`（事件通道）
- **数据源**：`mediaSourceManager_`（`MediaSourceManager`，持有 `IStream`）
- **管线**：`pipeline_`（`PlayPipeline`）
- **通知**：`playSession_`

### 状态机

状态流转由 `isValidTransition()` 校验，关键状态：

```
NotInited → Initializing → Prepared → Buffering → Ready → Playing ⇄ Pause
                                                              ↓
                                                          Completed → Stop
                                                              ↓
                                                            Error → Stop
```

`setState()` 负责：修改状态（线程安全）→ 校验合法性 → 触发 `onStateChanged()` → 通知外部观察者。

### 初始化流程

`init()` 主要步骤：

1. 构建 `NotifyCenter`、性能会话。
2. 初始化事件通道（`Channel<EventPtr>`）。
3. `setupDataProvider()`：调用 `mediaSourceManager_.open(uri, onData, onError, onComplete)`，在 `onData` 回调中将 `DataChunk` 转为 `DataPacket` 推给 `DemuxerComponent`。
4. `buildPipeline()`：创建 `PlayPipeline`，注入 `DemuxerComponent`，注册 Pipeline 事件回调。
5. 创建 `ownerThread_`，注册三个周期任务：
   - 10ms：`process()`（懒初始化 `pipeline_->build()` + 事件处理 + `drainPendingTasks()`）
   - 200ms：上报播放时间
   - 500ms：上报缓存时长
6. 启动 `dataProvider_->start()` 与 `ownerThread_->start()`。

### 主循环：`process()`

```
process()
  ├── pipeline_->build()（首次调用，在 ownerThread 上完成 build）
  ├── handleEvent(receiver_->tryReceiveAll())
  └── pipeline_->drainPendingTasks()
```

### 事件处理：`handleEvent()`

- `Seek`：`pipeline_->seek(CTime::fromSeconds(time))`
- `Prepared`：`preparePlayerInfo()`（创建解码器和渲染器）
- 设置更新：`handleSettingUpdate()`
- `RenderEnd`：检测循环/结束，切换状态

### Pipeline 事件处理：`handlePipelineEvent()`

| PipelineEvent                               | 动作                             |
| ------------------------------------------- | -------------------------------- |
| `DemuxHeaderParsed`                         | 发送 `EventType::Prepared` 事件  |
| `PipelineReady`                             | `setState(Ready)`                |
| `SeekCompleted`                             | `setState(Ready)`                |
| `AudioDecoderFailed` / `VideoDecoderFailed` | `setState(Error)`                |
| `ReadError`                                 | `setState(Error)`                |
| `RenderEnd`                                 | 发送 `EventType::RenderEnd` 事件 |

### prepare 阶段

`preparePlayerInfo()` 在 `DemuxHeaderParsed` 后执行：

1. 从 `DemuxerComponent` 提取媒体能力（是否有音视频、时长）。
2. `setState(Prepared)`。
3. 创建音频解码器 + `AudioRenderComponent`（注入 `pullDataFunc_`）。
4. 创建视频解码器。
5. `doPause()` + `setState(Buffering)`。

### 生命周期

- `doPlay()`：`pipeline_->start()` + `mediaSourceManager_.start()`
- `doPause()`：`pipeline_->pause()` + `mediaSourceManager_.pause()`
- `doStop()`：`mediaSourceManager_.close()` → `pipeline_->teardown()` → `ownerThread_->stop()`
- `doLoop()`：`pipeline_->seek(0)` → `mediaSourceManager_.start()` → `pipeline_->start()`

---

## PlayPipeline

### 线程归属

| 线程          | 职责                                                       |
| ------------- | ---------------------------------------------------------- |
| ownerThread_  | build/start/pause/seek/teardown，所有 onEvent 回调在此触发 |
| demuxThread   | DemuxerComponent 内部线程，负责 demux 循环                 |
| decoderThread | DecoderComponent 内部线程，负责解码循环                    |
| vsync         | 平台 vsync 回调线程，调用 `requestVideoFrame()`            |
| 音频系统线程  | AudioRenderComponent 内部线程，拉取音频帧                  |

### 状态机

```
Idle ──build()──→ Built ──start()──→ Playing ⇄ pause()/start()
                                        ↓
                              teardown()──→ Idle（任意态均可）
                                        ↓
                         join 超时 ──────→ Faulted（终态）
```

所有 API 在 `Faulted` 状态下均为 no-op 并上报 `ReadError`。

### 核心子系统

**SeekCoordinator**：管理 `seekEpoch_`（单调递增）。
- `beginSeek(target)` → epoch+1，返回是否覆盖旧 seek（触发 `SeekTimeout`）
- decoder receive callback 打上当前 epoch
- `popVideoFrame()` 丢弃 epoch 不匹配的旧帧
- seek 6 步顺序：pause → flush → seekStream → seekDemuxer → resetClocks → resume
- 首帧渲染后 `completeSeek()` 触发 `SeekCompleted`

**ClockSync**：音视频同步时钟，teardown 闸门。
- `setAudioTime()` / `setVideoTime()`：由 `requestAudioFrame()` / `requestVideoFrame()` 取帧后更新
- `masterTime(hasAudio)`：返回主时钟（有音频用音频时钟，否则用视频时钟）
- `notifyTeardown()`：置位 teardown 闸门，vsync 回调立即返回 nullptr
- `Metrics` 结构体：可观测指标（dropCount、chaseRoundCount、vsyncDurationUs）
- `recordDrop(count)` / `recordVsyncDuration(us)`：追帧和 vsync 耗时记录

**BufferMonitor**：缓冲区水位与卡顿检测。
- 预缓冲/重缓冲状态管理，触发 `PipelineReady` / `BufferingStart` / `BufferingEnd`
- `onStarvation(isLocalSource)`：网络播放连续无帧时累计计数，超 `kStarvationThreshold`(5) 触发 `BufferingStart`
- `resetStarvationCount()`：成功取帧后重置 starvation 计数

## perf 接入（P3）

**文件结构**：
```
src/core/perf/
├── public/
│   ├── PerfTypes.hpp       # 所有数据类型（PerfConfig/SessionReport/FrameTrace/ScopedSpan 等）
│   └── IPerfReporter.hpp   # 上报接口（onFrameTrace/onSessionComplete）
├── Perf.hpp                # 宏门面（SLARK_TRACE_MILESTONE/SLARK_TRACE_SPAN 等）
├── PerfMonitor.hpp/.cc     # 全局单例，管理 reporter/session/采样器
├── PlaySession.hpp/.cc     # 单次播放会话，记录 milestone/stage/frame/resource
└── ResourceSampler.hpp/.cc # 后台 CPU/内存采样线程
```

**PlayPipeline perf 埋点**（通过 `SLARK_TRACE_MILESTONE`，仅在 `SLARK_PERF_ENABLED` 时生效）：

| 埋点                  | 触发时机               |
| --------------------- | ---------------------- |
| `demux_header_parsed` | DemuxHeaderParsed 事件 |
| `audio_decoder_ready` | 音频 decoder open 成功 |
| `video_decoder_ready` | 视频 decoder open 成功 |
| `pipeline_ready`      | PipelineReady 事件     |
| `seek_start`          | seek() 入口            |
| `seek_completed`      | SeekCompleted 事件     |
| `first_video_frame`   | 首帧视频渲染（幂等）   |

**PlayerImpl 集成**：
- `buildPipeline()` 中调用 `pipeline_->setPlaySession(playSession_)` 注入会话
- `handlePipelineEvent()` 处理 `BufferingStart`/`BufferingEnd`/`SeekTimeout` 事件
- `notifyCacheTime()` 通过 `pipeline_->cachedDuration()` 上报缓冲时长
- `doStop()` 中调用 `playSession_->finish(success)` 触发 `PerfMonitor::reportSession()`
- `startPrebuffering()`：decoder 全部就绪后进入预缓冲状态；本地播放（`startBufferDuration=0`）立即通过 `onPrebufferComplete_` 回调触发 `PipelineReady`；网络播放等待 `demuxedDuration >= startBufferDuration` 后触发
- `onDataEnqueued()`：每次 demuxer 入队后触发背压检查（事件驱动 + 节流：本地 100ms，网络 50ms）
- `onHeartbeat()`：200ms 低频心跳，兜底 resume 检查，防止 pause 后入队事件稀少导致 resume 延迟
- `reset()`：seek 后重置所有状态
- 通过 `pauseStreamFunc_` / `resumeStreamFunc_` 回调控制 `MediaSourceManager::pause()` / `start()` 数据流速
- `demuxedDuration()`：通过 `audioFrames_` 队列尾部 pts 减去当前主时钟时间估算缓冲时长

**追帧上限保护**（`BackpressureConfig::maxDropPerVsync`）：
- 每次 vsync 拉帧时，若视频落后音频超过 `avSyncThresholdSec`（默认 0.1s），连续丢帧追赶
- 最多丢 `maxDropPerVsync` 帧（默认 8，直播 4），防止单次 vsync 耗时过长
- seek 期间跳过追帧逻辑，避免 seek 后误丢首帧

### 关键接口

| 接口                                            | 说明                                                                                |
| ----------------------------------------------- | ----------------------------------------------------------------------------------- |
| `build()`                                       | 记录 ownerThreadId_，连接 demuxer→decoder 回调链，幂等                              |
| `start()` / `pause()`                           | 启动/暂停所有组件，幂等                                                             |
| `flush()`                                       | 清空所有队列（audioFrames_/videoFrames_），重置 seek/ready/renderEnd 标志           |
| `seek(CTime)`                                   | 6 步 seek：pause → flush → seekStream → seekDemuxer → resetClocks → resume          |
| `teardown()`                                    | 有界收敛销毁（2s），超时进入 Faulted                                                |
| `requestVideoFrame()`                           | vsync 入口，try_lock 取帧 + AV Sync 追帧，metrics 记录，starvation 检测             |
| `requestAudioFrame()`                           | 音频系统拉帧，更新 audioTime，seek 完成检测                                         |
| `currentTime()`                                 | 统一时钟源：有音频返回 audioRender_->playedTime()，否则返回 clockSync_->videoTime() |
| `onSeekFrameConsumed(isVideo)`                  | seek 完成检查：轨道帧消费后标记对应 seek 完成，全部完成时触发事件                   |
| `drainPendingTasks()`                           | ownerThread 事件循环中消费跨线程投递的任务队列                                      |
| `createAudioDecoder()` / `createVideoDecoder()` | 创建 decoder 并绑定 receive callback                                                |
| `openDecoder()`                                 | 异步打开 decoder，结果投递到 ownerThread                                            |

### 数据流路径

```
IStream::onData → DemuxerComponent::pushData()
    → setHandleResultFunc 回调（demux 线程）
        → audioDecoder_->send() / videoDecoder_->send()（代际过滤）
    → DecoderComponent receive callback（decoder 线程）
        → audioFrames_ / videoFrames_（打 seekEpoch 标记）
    → AudioRenderComponent::pullDataFunc_ → requestAudioFrame()
    → IVideoRender::requestRenderFunc_ → requestVideoFrame()
```

### RenderEnd 触发逻辑

| 场景     | 触发位置                                   | 条件                                             |
| -------- | ------------------------------------------ | ------------------------------------------------ |
| 纯视频流 | `popVideoFrame()`                          | videoFrames_ 为空 && demuxer_->isCompleted()     |
| 有音频轨 | `AudioRenderComponent::renderEndCallback_` | isHungry_ 首次变 true && demuxer_->isCompleted() |

`renderEndFired_` 标志防止重复触发，`flush()` 时重置（seek 后可再次触发）。
`AudioRenderComponent::flush()` 同时重置 `isHungry_`，确保 seek 后回调能再次触发。

### postToOwnerThread 机制

跨线程回调通过 `postToOwnerThread(task)` 投递到 `pendingTasks_` 队列，ownerThread 在 `drainPendingTasks()` 中消费。若已在 ownerThread 则直接执行，teardown 后丢弃。

---

## 与周边模块的关系

- [IStream.hpp](/Users/nevermore-/selfspace/slark/src/base/IStream.hpp)：提供统一输入数据抽象
- [DemuxerComponent.hpp](/Users/nevermore-/selfspace/slark/src/core/demuxer/DemuxerComponent.hpp)：承接原始数据并产出音视频包
- [DecoderComponent.hpp](/Users/nevermore-/selfspace/slark/src/core/codec/DecoderComponent.hpp)：音视频解码
- [AudioRenderComponent.h](/Users/nevermore-/selfspace/slark/src/core/audio/AudioRenderComponent.h)：音频渲染，主时钟来源
- [VideoInfo.h](/Users/nevermore-/selfspace/slark/src/core/video/VideoInfo.h)：定义 `IVideoRender` 接口（vsync 拉帧唯一入口 `requestRenderFunc_`，已移除旧推帧接口）

---

## 一句话总结

`Player::Impl` 是状态机 + 事件循环的编排层；`PlayPipeline` 是数据链路的管线层，两者通过 Pipeline 事件回调协作，共同完成播放器运行时编排。