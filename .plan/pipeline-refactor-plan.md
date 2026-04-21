# PlayPipeline 重构设计方案

> 目标：将 `Player::Impl` 从"上帝类"变为"管线编排者"，让每个模块有清晰的边界和独立的生命周期。

---

## 一、现状问题

### 1. `Player::Impl` 职责过重

同时持有：状态机、事件循环、**6 个中间队列**、数据链路调度、生命周期管理、时间通知。  
`PipelineScheduler`、`PacketHandler`、`SeekHandler` 虽然文件上拆出去了，但都持有 `Player::Impl&` 或 `weak_ptr<Player::Impl>` 引用，本质上是 `Impl` 的"方法碎片"，属于**伪解耦**。

### 2. 中间队列散落在 `Impl`，边界模糊

`audioPackets_`、`videoPackets_`、`audioFrames_`、`videoFrames_` 直接挂在 `Impl` 上，三个子系统都通过 `impl_` 直接操作，形成隐式耦合。

### 3. 10ms 轮询驱动整条链路

`ownerThread_` 每 10ms 执行 `process()`，串行调用 `demuxData()` → `pushAVFrameDecode()` → `pushAVFrameToRender()`。  
`DemuxerComponent` 和 `DecoderComponent` 各自有独立工作线程，但推入数据的节奏却由外部轮询决定，存在不必要的延迟。

### 4. `DecoderComponent::open()` 异步无错误回传

`open()` 内部异步创建解码器，失败只打 log，上层无法感知，导致静默失败。

### 5. seek 流程分散

seek 涉及 `SeekHandler`、`DemuxerComponent::seekToPos()`、`dataProvider_->seek()`、`audioRender_->seek()`、`videoRender_->setTime()`，散落在多处，难以追踪完整流程。

### 6. AV Sync 逻辑不内聚

`requestRender()` 在 `Drop` 时采用"回退时间戳"的方式（`videoTime -= frameDuration`）绕过追帧，逻辑不直观；追帧依赖外部 10ms 定时器多次触发，视频落后严重时追赶速度慢。

### 7. `IStream` 缺少背压控制

`onData` 回调直接写 `dataList_`，当 demux 处理慢时队列无限增长。

### 8. Render 模块职责混乱

**视频侧**：`IVideoRender` 同时存在两条帧消费路径——`pushVideoFrameRender()`（推帧，ownerThread 10ms 驱动）和 `requestRenderFunc_`（拉帧，vsync 驱动），两条路径并存，逻辑重复。视频时钟（`videoClock_`）的更新分散在两处：推帧路径在 `PipelineScheduler::pushVideoFrameToRender()` 中调用 `render->setTime()`，拉帧路径在 `PipelineScheduler::requestRender()` 中调用 `impl_.setVideoRenderTime()`，不统一。`IVideoRender` 还内嵌了 `videoClock_`，使平台层实现承担了不该有的时钟职责。

**音频侧**：`AudioRenderComponent` 渲染结束需要外部轮询 `isHungry()` 后主动调用 `renderEnd()`，`PipelineScheduler::pushAudioFrameToRender()` 中有专门的判断逻辑，感知方式被动。

---

## 二、目标架构

```
┌─────────────────────────────────────────────────────┐
│                   Player::Impl（编排层）              │
│   StateMachine  │  EventLoop  │  NotifyCenter        │
│                 LifecycleCoordinator                 │
└──────────────────────┬──────────────────────────────┘
                       │ build / start / pause / teardown
                       ▼
┌─────────────────────────────────────────────────────┐
│                   PlayPipeline（管线层）              │
│                                                     │
│  IStream → [dataQueue] → DemuxerComponent           │
│         → [audioPackets] → DecoderComponent(Audio)  │
│         → [videoPackets] → DecoderComponent(Video)  │
│         → [audioFrames]  → AudioRender              │
│         → [videoFrames]  → IVideoRender             │
│                                                     │
│  PipelineScheduler（内部调度，不对外暴露）             │
│  ClockSync        （AV 同步时钟）                     │
│  SeekCoordinator  （seek 协同，单一入口）              │
│  BufferMonitor    （背压控制）                        │
└─────────────────────────────────────────────────────┘
```

---

## 三、关键模块设计

### 3.1 `PlayPipeline`

**职责**：持有数据链路的所有组件和队列，对外暴露生命周期接口和事件回调。

```cpp
// PlayPipeline.h
class PlayPipeline {
public:
    explicit PlayPipeline(const PipelineConfig& config) noexcept;
    ~PlayPipeline();

    bool build() noexcept;
    void start() noexcept;
    void pause() noexcept;
    void flush() noexcept;
    void teardown() noexcept;

    void seek(double time, bool isAccurate) noexcept;

    [[nodiscard]] bool isRenderCompleted() const noexcept;
    [[nodiscard]] double currentTime() const noexcept;

    // 向上通知事件，携带可选错误上下文
    std::function<void(PipelineEvent, std::optional<PipelineError>)> onEvent;

private:
    // 组件
    std::shared_ptr<IStream>              stream_;
    std::shared_ptr<DemuxerComponent>     demuxer_;
    std::shared_ptr<DecoderComponent>     audioDecoder_;
    std::shared_ptr<DecoderComponent>     videoDecoder_;
    std::unique_ptr<AudioRenderComponent> audioRender_;
    AtomicWeakPtr<IVideoRender>           videoRender_;

    // 队列（归属 Pipeline，不再散落在 Impl）
    Synchronized<std::deque<DataPacket>>                        dataQueue_;
    Synchronized<std::deque<AVFramePtr>>                        audioPackets_;
    Synchronized<std::deque<AVFramePtr>>                        videoPackets_;
    Synchronized<std::deque<AVFrameRefPtr>>                     audioFrames_;
    // 视频帧队列：元素携带代际标记；有 B 帧时按 PTS 排序，使用 multimap 避免同 PTS 覆盖
    Synchronized<std::multimap<int64_t, TaggedFrame>>           videoFrames_;

    // 媒体信息，build() 后由 DemuxerComponent 填充
    MediaInfo info_;

    // 内部子系统
    std::unique_ptr<PipelineScheduler> scheduler_;
    std::unique_ptr<SeekCoordinator>   seekCoordinator_;
    std::unique_ptr<BufferMonitor>     bufferMonitor_;
    std::unique_ptr<ClockSync>         clockSync_;
};
```

**关键变化**：
- `Player::Impl` 不再直接持有任何队列和组件
- `PipelineScheduler` / `PacketHandler` 操作 `PlayPipeline` 内部队列，而非 `Impl`
- `ClockSync` 作为 `PlayPipeline` 的内部子系统，持有 `videoFrames_` 的访问权

---

### 3.2 `Player::Impl` 瘦身后

```cpp
// PlayerImpl.h（瘦身后）
class Player::Impl {
public:
    void init() noexcept;
    void updateState(PlayerState state) noexcept;
    void seek(double time, bool isAccurate) noexcept;
    // ... 对外接口不变

private:
    // 状态机
    Synchronized<PlayerState, std::shared_mutex> state_;

    // 事件循环
    std::shared_ptr<Thread>  ownerThread_;
    SenderPtr<EventPtr>      sender_;
    ReceiverPtr<EventPtr>    receiver_;

    // 管线（唯一的数据链路持有者）
    std::unique_ptr<PlayPipeline> pipeline_;

    // 基础设施
    std::unique_ptr<NotifyCenter>      notifyCenter_;
    std::shared_ptr<perf::PlaySession> playSession_;

    // 基础信息
    std::string playerId_;
    Synchronized<std::unique_ptr<PlayerParams>, std::shared_mutex> params_;
    PlayerInfo info_;
};
```

`Impl` 只做：**状态机 + 事件循环 + 生命周期协调 + 对外通知**。

`ownerThread_` 的定时任务精简为：

```
10ms  → handleEvent()           事件循环
200ms → notifyPlayedTime()      播放时间上报
500ms → pipeline_->tick()       缓存检查（BufferMonitor）
```

---

### 3.3 `ClockSync`（新增，替换 `PipelineScheduler` 中的 AV Sync 逻辑）

**职责**：以音频时钟为基准，驱动视频帧的拉取与追帧，是 vsync 拉帧路径的唯一决策者。

> 详细设计参见 [av-sync-architecture-overview.md](/Users/nevermore-/selfspace/slark/.doc/av-sync-architecture-overview.md)

```cpp
// ClockSync.h
class ClockSync {
public:
    explicit ClockSync(PlayPipeline& pipeline) noexcept;

    /// vsync 回调入口，返回本次应渲染的帧，nullptr 表示跳过本次渲染
    [[nodiscard]] AVFrameRefPtr requestVideoFrame() noexcept;

    void setAudioRenderEnd(bool isEnd) noexcept;
    void reset() noexcept;

private:
    [[nodiscard]] double audioTime() const noexcept;
    [[nodiscard]] double videoTime() const noexcept;
    void setVideoTime(double pts) noexcept;

    [[nodiscard]] AVFrameRefPtr popVideoFrame() noexcept;
    void handleVideoDecodeEnd() noexcept;

    [[nodiscard]] AVSyncAction evaluateAVSync(double diff) const noexcept;

    PlayPipeline&       pipeline_;
    ClockSyncConfig     config_;
    ClockSyncMetrics    metrics_;
    std::atomic<double> videoTime_{0.0};
    std::atomic<bool>   teardownFlag_{false};  // teardown 闸门，防止悬空回调
    bool                isAudioRenderEnd_{false};
};
```

**`requestVideoFrame` 核心逻辑**：

```cpp
AVFrameRefPtr ClockSync::requestVideoFrame() noexcept {
    // teardown 闸门：对象销毁后立即返回，防止悬空回调
    if (teardownFlag_.load(std::memory_order_acquire)) return nullptr;

    double diff = audioTime() - videoTime();
    if (isAudioRenderEnd_) diff = 0;

    // 视频超前：等待音频追上
    if (evaluateAVSync(diff) == AVSyncAction::Hold) {
        return nullptr;
    }

    auto frame = popVideoFrame();
    if (!frame) {
        handleVideoDecodeEnd();
        return nullptr;
    }
    setVideoTime(frame->ptsTime());

    // 追赶：连续消费过时帧，直到追上或无帧可取
    // 低帧率（fps < vsync）：队列无积压，循环立即退出，不丢帧
    // 高帧率（fps >= vsync）：队列有积压，丢弃中间帧追赶
    int32_t dropCount = 0;
    const auto deadline = Clock::now() + std::chrono::microseconds(config_.maxSyncWorkTimeUs);
    diff = audioTime() - videoTime();
    while (evaluateAVSync(diff) == AVSyncAction::Drop
           && dropCount < config_.maxDropPerVsync
           && Clock::now() < deadline) {
        auto nextFrame = popVideoFrame();
        if (!nextFrame) break;
        frame = std::move(nextFrame);
        setVideoTime(frame->ptsTime());
        diff = audioTime() - videoTime();
        ++dropCount;
    }

    metrics_.recordDrop(dropCount);
    return frame;
}
```

**视频帧率与 vsync 频率的关系**：

| 场景                                               | 追赶策略                                                        |
| -------------------------------------------------- | --------------------------------------------------------------- |
| 视频帧率 < vsync 频率（如 30fps 视频，60fps 屏幕） | 有空闲 vsync，每次 vsync 多消费一帧即可追上，不丢帧             |
| 视频帧率 ≥ vsync 频率（如 60fps 视频，60fps 屏幕） | 无空闲 vsync，必须在单次 vsync 内消费多帧（丢弃中间帧）才能追上 |

低帧率场景下，`while` 追赶循环因帧队列为空而自然退出，无需显式区分两种场景，逻辑统一。

---

### 3.4 `SeekCoordinator`（替换 `SeekHandler`）

**职责**：seek 的唯一入口，步骤清晰，顺序可追踪。

```cpp
// SeekCoordinator.h
class SeekCoordinator {
public:
    explicit SeekCoordinator(PlayPipeline& pipeline) noexcept;

    /// 发起 seek，新请求直接覆盖旧请求（最后一次生效语义）
    /// 若当前正在执行 seek，先递增 generationId 使旧解码结果失效，再执行新 seek
    void seek(double time, bool isAccurate) noexcept;
    void onVideoFrameRendered() noexcept;   // seek 完成回调触发点

    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] std::optional<double> activeSeekTime() const noexcept;
    [[nodiscard]] bool isForceVideoRendered() const noexcept;
    [[nodiscard]] int32_t fastPushDecodeCount() const noexcept;
    [[nodiscard]] uint32_t currentGeneration() const noexcept;
    void consumeFastPushCount() noexcept;

private:
    // seek 步骤，顺序明确，全部在 ownerThread_ 串行执行
    void pausePipeline() noexcept;          // step1
    void flushQueues() noexcept;            // step2
    void seekStream(double time) noexcept;  // step3: IStream::seek
    void seekDemuxer(double time) noexcept; // step4: DemuxerComponent::seekToPos
    void resetClocks(double time) noexcept; // step5: AudioRender + ClockSync::reset
    void resumePipeline() noexcept;         // step6

    struct SeekRequest {
        double   seekTime{0};
        bool     isAccurate{false};
    };

    PlayPipeline&                pipeline_;
    AtomicSharedPtr<SeekRequest> request_;
    std::atomic<uint64_t>        generationId_{0};  // 代际 token，seek 时递增；uint64_t 防回绕
    PlayerState                  stateBeforeSeek_{PlayerState::Unknown};
    bool                         isForceVideoRendered_{false};
    int32_t                      fastPushDecodeCount_{0};
};
```

**关键变化**：
- seek 流程从"散落在 `SeekHandler` + `clearData()` + `ComponentFactory`"变为**单一入口、6 步顺序执行**
- `SeekCoordinator` 持有 `PlayPipeline&`，而非 `Player::Impl&`
- `resetClocks` 中同时重置 `AudioRender` 时钟和 `ClockSync::reset()`，保证 AV sync 状态干净

---

### 3.5 `DecoderComponent::open()` 错误回调

当前 `open()` 异步创建解码器，失败只打 log，上层无法感知。

```cpp
// 现状：异步无回传
void open(DecoderType type, const std::shared_ptr<DecoderConfig>& config) noexcept;

// 改为：异步 + 完成回调，携带错误上下文
void open(DecoderType type,
          const std::shared_ptr<DecoderConfig>& config,
          std::function<void(DecoderOpenResult)> onOpened) noexcept;
```

`PlayPipeline::build()` 中：

```cpp
// 回调投递到 ownerThread_ 事件队列，保证串行执行
audioDecoder_->open(type, config, [this](DecoderOpenResult result) {
    postToOwnerThread([this, result]() {
        if (!result.success) {
            onEvent(PipelineEvent::AudioDecoderFailed,
                    PipelineError{PipelineEvent::AudioDecoderFailed,
                                  static_cast<int32_t>(result.error), result.message});
        }
        checkAndFirePipelineReady();
    });
});
videoDecoder_->open(type, config, [this](DecoderOpenResult result) {
    postToOwnerThread([this, result]() {
        if (!result.success) {
            onEvent(PipelineEvent::VideoDecoderFailed,
                    PipelineError{PipelineEvent::VideoDecoderFailed,
                                  static_cast<int32_t>(result.error), result.message});
        }
        checkAndFirePipelineReady();
    });
});
```

`checkAndFirePipelineReady()` 按实际轨道计数，不要求不存在的 decoder 成功：

```cpp
void PlayPipeline::checkAndFirePipelineReady() noexcept {
    // 只统计实际存在的轨道
    const int32_t expected = (info_.hasAudio ? 1 : 0) + (info_.hasVideo ? 1 : 0);
    if (++decoderReadyCount_ >= expected) {
        // once-gate：防止重试 open 或重复回调导致多次触发 PipelineReady
        bool expected_fired = false;
        if (readyFired_.compare_exchange_strong(expected_fired, true,
                                                std::memory_order_acq_rel)) {
            onEvent(PipelineEvent::PipelineReady, std::nullopt);
        }
    }
}
```

`readyFired_` 为 `std::atomic<bool>` 成员，`build()` 时初始化为 `false`，`flush()` 后重置为 `false`（允许重新就绪）。

---

### 3.6 `IStream` 背压控制

`IStream` 接口已有 `pause()` / `start()`，`start()` 语义等同于 resume，**无需改名**。

`BufferMonitor` 检测 `dataQueue_` 水位，超过阈值时调用 `stream_->pause()`，低于阈值时 `stream_->start()`。

```cpp
// BufferMonitor 改为持有 PlayPipeline& 而非 Player::Impl&
class BufferMonitor {
public:
    explicit BufferMonitor(PlayPipeline& pipeline) noexcept;

    void checkCacheState() noexcept;
    void regulateDataFlow() noexcept;   // 背压控制：检测 dataQueue_ 水位

    [[nodiscard]] double demuxedDuration() const noexcept;
    void resetDemuxProgress(double t = 0.0) noexcept;
    void setAudioDemuxedTime(double t) noexcept;
    void setVideoDemuxedTime(double t) noexcept;

private:
    PlayPipeline& pipeline_;
    std::atomic<double> audioDemuxedTime_ = 0;
    std::atomic<double> videoDemuxedTime_ = 0;
};
```

---

### 3.7 `PipelineScheduler` 去除 `Impl` 引用

```cpp
// 现状
class PipelineScheduler {
    Player::Impl& impl_;
};

// 改为
class PipelineScheduler {
    PlayPipeline& pipeline_;
};
```

内部操作 `pipeline_.audioPackets_`、`pipeline_.videoFrames_` 等，不再穿透到 `Impl`。

AV Sync 判断逻辑（`evaluateAVSync`）从 `PipelineScheduler` 中移除，统一由 `ClockSync` 负责。

---

### 3.9 Render 模块重设计

#### 3.9.1 `IVideoRender` 接口精简

**现状问题**：
- `IVideoRender` 内嵌 `videoClock_`，平台层实现需要关心时钟管理
- 推帧路径（`pushVideoFrameRender`）和拉帧路径（`requestRenderFunc_`）并存，逻辑重复
- 视频时钟更新分散在两处，不统一

**重构方向**：统一为**拉帧路径**，`IVideoRender` 只负责渲染，时钟由 `ClockSync` 统一管理。

```cpp
// IVideoRender（精简后）
struct IVideoRender {
    virtual void start() noexcept = 0;
    virtual void pause() noexcept = 0;
    virtual void notifyVideoInfo(std::shared_ptr<VideoInfo> videoInfo) noexcept = 0;
    virtual void notifyRenderInfo() noexcept = 0;
    virtual void renderEnd() noexcept = 0;
    virtual ~IVideoRender() = default;

    /// vsync 回调唯一入口，内部调用 requestRenderFunc_（即 ClockSync::requestVideoFrame()）
    [[nodiscard]] AVFrameRefPtr requestFrame() noexcept {
        if (auto func = requestRenderFunc_.load()) {
            return std::invoke(*func);
        }
        return nullptr;
    }

    void setRequestRenderFunc(RequestRenderFunc func) noexcept {
        requestRenderFunc_.reset(std::make_shared<RequestRenderFunc>(std::move(func)));
    }

protected:
    AtomicSharedPtr<RequestRenderFunc> requestRenderFunc_;
    std::string playerId_;
};
```

**关键变化**：
- 移除 `pushVideoFrameRender()`：推帧路径废弃，统一走 vsync 拉帧
- 移除 `videoClock_` 和 `setTime()`：视频时钟职责转移到 `ClockSync`
- `requestFrame()` 是平台层 vsync 回调的唯一入口

#### 3.9.2 视频时钟归属迁移

`videoClock_` 从 `IVideoRender` 迁移到 `ClockSync::videoTime_`（`std::atomic<double>`），由 `ClockSync::setVideoTime()` 在每次 `requestVideoFrame()` 取帧后更新，时钟更新路径唯一。

`PlayPipeline::currentTime()` 实现：

```cpp
double PlayPipeline::currentTime() const noexcept {
    if (info_.hasAudio && audioRender_) {
        return audioRender_->playedTime().seconds();
    }
    if (info_.hasVideo && clockSync_) {
        return clockSync_->videoTime();
    }
    return 0.0;
}
```

#### 3.9.3 `AudioRenderComponent` 渲染结束感知

**现状问题**：音频渲染结束需要外部轮询 `isHungry()` 后主动调用 `renderEnd()`，`PipelineScheduler::pushAudioFrameToRender()` 中有专门的判断逻辑。

**重构方向**：`AudioRenderComponent` 内部感知渲染结束，通过回调通知上层。

```cpp
// AudioRenderComponent 新增
void setRenderEndCallback(std::function<void()> callback) noexcept;
```

`PlayPipeline::build()` 中注册：

```cpp
audioRender_->setRenderEndCallback([this]() {
    clockSync_->setAudioRenderEnd(true);
    onEvent(PipelineEvent::AudioRenderEnd);
});
```

`PipelineScheduler::pushAudioFrameToRender()` 中的 `isHungry()` 轮询判断随之删除。

#### 3.9.4 `PipelineScheduler` 简化

废弃 `pushVideoFrameToRender()` 后，`pushAVFrameToRender()` 只保留音频推帧：

```cpp
void PipelineScheduler::pushAVFrameToRender() noexcept {
    // 视频帧由 ClockSync::requestVideoFrame() 在 vsync 回调中拉取，此处不再推帧
    if (info_.hasAudio) {
        pushAudioFrameToRender();
    }
}
```

`requestRender()` 从 `PipelineScheduler` 中删除，统一由 `ClockSync::requestVideoFrame()` 承接。

---

### 3.8 `PacketHandler` 去除 `weak_ptr<Impl>` 引用

```cpp
// 现状
class PacketHandler {
    std::weak_ptr<Player::Impl> player_;
};

// 改为
class PacketHandler {
    PlayPipeline& pipeline_;
};
```

`handleAudioPacket` / `handleVideoPacket` 直接操作 `pipeline_.audioPackets_` / `pipeline_.videoPackets_`，`seekToLastAvailableKeyframe` 通过 `pipeline_.seekCoordinator_` 协同。

---

## 四、`PipelineEvent` 与 `PipelineError` 定义

```cpp
enum class PipelineEvent : uint8_t {
    DemuxHeaderParsed,    // demuxer 解析头完成，媒体信息（分辨率/时长/轨道）可读
    PipelineReady,        // 所有存在轨道的 decoder 就绪，可安全调用 start()
    AudioDecoderFailed,   // 音频解码器创建失败
    VideoDecoderFailed,   // 视频解码器创建失败
    AudioRenderEnd,       // 音频渲染结束
    VideoRenderEnd,       // 视频渲染结束
    BufferingStart,       // 缓冲开始
    BufferingEnd,         // 缓冲结束
    ReadError,            // 流读取错误
    SeekCompleted,        // seek 完成（首帧已渲染且代际匹配）
    SeekTimeout,       // seek 被后续 seek 覆盖，业务侧可忽略等待
    RenderEnd,            // 音视频均渲染结束（播放完毕）
};

/// 错误上下文，随 onEvent 回调透传给上层
enum class PipelineStage : uint8_t {
    Demux,        // demux 阶段
    AudioDecode,  // 音频解码阶段
    VideoDecode,  // 视频解码阶段
    AudioRender,  // 音频渲染阶段
    VideoRender,  // 视频渲染阶段
    BackPressure, // 背压控制阶段
    Seek,         // seek 阶段
};

struct PipelineError {
    PipelineEvent event;
    PipelineStage stage;    // 发生错误的阶段，便于上层区分来源
    int32_t       code;     // 平台错误码
    std::string   message;
};

/// onEvent 签名：所有事件统一走此回调，错误事件携带 PipelineError
/// 示例：onEvent(PipelineEvent::AudioDecoderFailed, PipelineError{...})
///        onEvent(PipelineEvent::PipelineReady, std::nullopt)
using PipelineEventCallback = std::function<void(PipelineEvent, std::optional<PipelineError>)>;
```

**事件契约统一表**：

| 事件                 | 触发时机                          |    `PipelineError` 必填     | 上层恢复策略建议                                             |
| -------------------- | --------------------------------- | :-------------------------: | ------------------------------------------------------------ |
| `DemuxHeaderParsed`  | demuxer 解析头完成                |             否              | 读取 `MediaInfo`，更新 UI 元数据                             |
| `PipelineReady`      | 所有存在轨道的 decoder 就绪       |             否              | 可安全调用 `start()`                                         |
| `AudioDecoderFailed` | 音频解码器创建失败                | **是**（stage=AudioDecode） | 降级为静音播放或上报错误终止                                 |
| `VideoDecoderFailed` | 视频解码器创建失败                | **是**（stage=VideoDecode） | 降级为纯音频播放或上报错误终止                               |
| `ReadError`          | 流读取/背压失控/实例 Faulted      | **是**（stage 按来源填写）  | 根据 `stage` 决策：BackPressure 可重试，Demux/Faulted 应重建 |
| `BufferingStart`     | 网络卡顿 / hardCap 超限触发       |             否              | 显示 loading 动画，暂停进度更新                              |
| `BufferingEnd`       | rebuffer 完成，可继续播放         |             否              | 隐藏 loading，恢复进度更新                                   |
| `SeekCompleted`      | 首帧渲染且 generationId 匹配      |             否              | 更新进度条到目标位置                                         |
| `SeekTimeout`     | seek 被后续 seek 覆盖 / seek 超时 |             否              | 忽略旧 seek 等待，以最新 seek 为准                           |
| `AudioRenderEnd`     | 音频渲染结束                      |             否              | 内部使用，上层通常不需处理                                   |
| `VideoRenderEnd`     | 视频渲染结束                      |             否              | 内部使用，上层通常不需处理                                   |
| `RenderEnd`          | 音视频均渲染结束（播放完毕）      |             否              | 触发播放完成回调，更新 UI 状态                               |

> **规则**：`PipelineError` 必填的事件，实现层**禁止**传 `std::nullopt`；非必填事件**禁止**传非空 `PipelineError`（保持契约单向，便于上层 `assert` 校验）。

**两阶段 Ready 语义**：
- `DemuxHeaderParsed`：demuxer 解析头完成，上层可读取媒体信息（分辨率、时长、轨道），但**不可调用 `start()`**
- `PipelineReady`：所有实际存在轨道的 decoder 均就绪，上层可安全调用 `start()`
- 单轨媒体（仅音频或仅视频）：`checkAndFirePipelineReady()` 只统计实际存在的轨道，不等待不存在的 decoder

---

## 五、文件结构变更

```
src/core/
├── PlayerImpl.h / .cpp          # 瘦身：只保留状态机 + 事件循环
├── pipeline/
│   ├── PlayPipeline.h / .cpp    # 新增：管线自治单元
│   ├── PipelineConfig.h         # 新增：管线构建配置
│   ├── PipelineEvent.h          # 新增：管线事件枚举
│   ├── PipelineScheduler.h/.cpp # 迁移：引用改为 PlayPipeline&
│   ├── ClockSync.h / .cpp       # 新增：AV 同步时钟，替换 PipelineScheduler 中的 sync 逻辑
│   ├── SeekCoordinator.h / .cpp # 新增：替换 SeekHandler
│   ├── PacketHandler.h / .cpp   # 迁移：引用改为 PlayPipeline&
│   └── BufferMonitor.h / .cpp   # 迁移：引用改为 PlayPipeline&
├── codec/                       # 不变（DecoderComponent::open 加 callback）
├── demuxer/                     # 不变
├── audio/                       # AudioRenderComponent 新增 setRenderEndCallback
├── video/
│   └── VideoInfo.h              # IVideoRender 精简：移除 videoClock_、pushVideoFrameRender、setTime
└── ...
```

---

## 六、模块依赖关系

```
Player::Impl
    │
    └── PlayPipeline
            ├── IStream
            ├── DemuxerComponent
            ├── DecoderComponent (audio/video)
            ├── AudioRenderComponent      ← 音频主时钟来源，内部感知渲染结束
            ├── IVideoRender (AtomicWeakPtr) ← 精简：只负责渲染，不持有时钟
            ├── PipelineScheduler  ──→ PlayPipeline&  (只推音频帧，视频帧由 ClockSync 拉)
            ├── ClockSync          ──→ PlayPipeline&  (持有 videoTime_，vsync 拉帧唯一入口)
            ├── SeekCoordinator    ──→ PlayPipeline&
            ├── PacketHandler      ──→ PlayPipeline&
            └── BufferMonitor      ──→ PlayPipeline&
```

**时钟归属**：
- 音频时钟：`AudioRenderComponent::playedTime()`（底层 `IAudioRender` 驱动，精度高）
- 视频时钟：`ClockSync::videoTime_`（每次 vsync 拉帧后更新，不再散落在 `IVideoRender`）

所有内部子系统只持有 `PlayPipeline&`，不再穿透到 `Player::Impl`，边界清晰。

---

## 七、实施优先级

| 优先级 | 改动                                                                                       | 收益                         | 风险                 |
| ------ | ------------------------------------------------------------------------------------------ | ---------------------------- | -------------------- |
| **P0** | 引入 `PlayPipeline`，队列归属内聚，`Impl` 瘦身                                             | 消除上帝类，边界清晰         | 改动面大，需整体重构 |
| **P1** | `ClockSync` 替换 `PipelineScheduler` 中的 AV Sync 逻辑                                     | 追帧内聚、vsync 驱动、不丢帧 | 中等                 |
| **P1** | `SeekCoordinator` 替换 `SeekHandler`                                                       | seek 流程单一入口，可追踪    | 中等                 |
| **P1** | `DecoderComponent::open()` 加错误回调                                                      | 消除静默失败                 | 小                   |
| **P2** | `PipelineScheduler` / `PacketHandler` / `BufferMonitor` 去除 `Impl` 引用                   | 真正解耦                     | 中等                 |
| **P2** | `IVideoRender` 精简：移除 `videoClock_` / `pushVideoFrameRender` / `setTime`，废弃推帧路径 | 时钟归属清晰，路径统一       | 中等                 |
| **P2** | `AudioRenderComponent` 新增 `setRenderEndCallback`，内部感知渲染结束                       | 消除外部轮询 `isHungry()`    | 小                   |
| **P3** | `IStream` 背压：`BufferMonitor::regulateDataFlow()` 控制 `stream_->pause/start`            | 防止 dataQueue 无限增长      | 小                   |

---

## 八、P0 实施步骤（详细）

1. **新建 `src/core/pipeline/` 目录**，创建 `PipelineEvent.h`、`PipelineConfig.h`
2. **创建 `PlayPipeline.h / .cpp`**，将 `Impl` 中的 6 个队列和组件成员迁移进来
3. **`ComponentFactory` 逻辑内化为 `PlayPipeline::build()`**，不再依赖 `Impl`
4. **`PipelineScheduler` 引用从 `Impl&` 改为 `PlayPipeline&`**，更新所有成员访问路径
5. **`PacketHandler` 引用从 `weak_ptr<Impl>` 改为 `PlayPipeline&`**，同上
6. **`BufferMonitor` 引用从 `Impl&` 改为 `PlayPipeline&`**，同上
7. **`Player::Impl` 删除队列和组件成员**，改为持有 `unique_ptr<PlayPipeline>`
8. **`Impl::process()` 改为转发给 `pipeline_->tick()`**（或由 Pipeline 内部自驱）
9. **更新 `CMakeLists.txt`**，加入 `pipeline/` 子目录

## 九、P1 实施步骤：ClockSync

1. **新建 `src/core/pipeline/ClockSync.h / .cpp`**
2. **实现 `requestVideoFrame()`**：vsync 驱动的拉帧 + 内部追帧循环（见 3.3 节）
3. **`PlayPipeline` 持有 `unique_ptr<ClockSync>`**，`PlayPipeline::build()` 中将 `ClockSync::requestVideoFrame` 注册为 `IVideoRender::requestRenderFunc_`
4. **删除 `PipelineScheduler` 中的 `evaluateAVSync` 和 `requestRender`**，AV Sync 判断统一由 `ClockSync` 负责
5. **`SeekCoordinator::resetClocks()` 中调用 `clockSync_->reset()`**，保证 seek 后 AV sync 状态干净

## 十、P2 实施步骤：Render 模块精简

1. **`IVideoRender` 移除 `videoClock_`、`setTime()`、`pushVideoFrameRender()`**，保留 `requestFrame()` 作为 vsync 唯一入口
2. **`PipelineScheduler::pushVideoFrameToRender()` 删除**，`pushAVFrameToRender()` 只保留音频推帧
3. **`ClockSync` 新增 `videoTime()` 公开访问**，供 `PlayPipeline::currentTime()` 使用
4. **`AudioRenderComponent` 新增 `setRenderEndCallback()`**，内部在 `isHungry_` 触发时调用回调
5. **`PipelineScheduler::pushAudioFrameToRender()` 删除 `isHungry()` 轮询判断**，渲染结束由回调驱动
6. **`PlayPipeline::build()` 中注册 `audioRender_->setRenderEndCallback()`**，回调中更新 `ClockSync` 状态并发出 `PipelineEvent::AudioRenderEnd`

---

## 十一、关键风险与缺口

### 11.1 并发模型：线程归属表

当前方案明确了模块职责，但未定义"谁在哪个线程运行、哪些 API 必须串行、哪些可并发"，`ownerThread_`、demux/decoder 线程、vsync 回调线程会形成竞态。

**线程归属表**：

| 模块 / API                                            | 调用线程                     | 回调/完成线程                   | 可重入性                             |
| ----------------------------------------------------- | ---------------------------- | ------------------------------- | ------------------------------------ |
| `PlayPipeline::build/start/pause/seek/flush/teardown` | `ownerThread_`（串行执行器） | —                               | 不可重入，必须串行                   |
| `DemuxerComponent::demux()`                           | demuxThread（内部）          | `ownerThread_`（onPacket 回调） | 内部串行                             |
| `DecoderComponent::decode()`                          | decoderThread（内部）        | `ownerThread_`（onFrame 回调）  | 内部串行                             |
| `AudioRenderComponent::pullData()`                    | 系统音频线程                 | —                               | 必须无锁或极短临界区                 |
| `ClockSync::requestVideoFrame()`                      | vsync 线程（平台回调）       | —                               | **不得阻塞**，锁等待预算 < 1ms       |
| `SeekCoordinator::seek()`                             | `ownerThread_`               | —                               | last-write-wins，新 seek 覆盖旧 seek |
| `BufferMonitor::checkCacheState()`                    | `ownerThread_`（500ms 定时） | —                               | 串行                                 |

**`PlayPipeline` 串行执行器约束**：`build/start/pause/seek/flush/teardown` 全部通过 `ownerThread_` 的事件队列串行执行，禁止跨线程直接调用。

**`onEvent` 线程约束**：`onEvent` 只能在 `ownerThread_` 上触发，其他线程只能通过 `postToOwnerThread()` 投递，禁止在 demux/decoder/vsync 线程直接调用 `onEvent`。

**`ClockSync::requestVideoFrame()` 约束**：vsync 线程不得被阻塞，内部对 `videoFrames_` 的访问必须使用 try_lock，超时直接返回 `nullptr`（跳过本次渲染），不等待。

**try_lock 连续失败退化策略**：单次 try_lock 失败是正常的（ownerThread 正在写队列），但若连续 N 次（建议 N=3，约 3 个 vsync 周期）失败，说明锁竞争异常，退化为**短预算自旋 + 无阻塞返回**（不使用 `lock_for`，避免在高刷新率下占用过多帧预算）。自旋预算与刷新率关联：60Hz 下 ≤500μs，120Hz 下 ≤250μs。连续失败计数在成功取帧后重置。

```cpp
AVFrameRefPtr ClockSync::popVideoFrame() noexcept {
    auto locked = pipeline_.videoFrames_.try_lock();
    if (!locked) {
        if (++tryLockFailCount_ >= kMaxTryLockFail) {
            // 退化：短预算自旋，预算与刷新率关联，不阻塞 vsync 线程
            const auto spinDeadline = Clock::now()
                + std::chrono::microseconds(config_.tryLockSpinBudgetUs);
            while (Clock::now() < spinDeadline) {
                locked = pipeline_.videoFrames_.try_lock();
                if (locked) break;
            }
            tryLockFailCount_ = 0;
        }
        if (!locked) return nullptr;
    }
    tryLockFailCount_ = 0;
    return extractFrontFrame(locked);
}
```

`tryLockSpinBudgetUs` 默认值：60Hz → 500，90Hz → 333，120Hz → 250，由 `ClockSyncConfig` 持有，可在 `IVideoRender` 上报刷新率时动态更新。

---

### 11.2 seek 代际隔离
**风险**：seek 后仍可能渲染旧帧（旧包回流）。seek 分 6 步，但未定义如何丢弃 seek 前残留的异步解码结果。`IStream → dataQueue_` 的旧数据回流边界也需要覆盖，否则旧数据仍可能被 demux 后进入解码队列。

**方案**：在 packet/frame 上附 `generationId`，seek 时递增，渲染前强校验；`dataQueue_` 入队口同样校验代际。

```cpp
/// 视频帧携带代际标记，存入 videoFrames_ 队列
struct TaggedFrame {
    AVFrameRefPtr frame;
    uint32_t      generationId;
};

// SeekCoordinator 中（generationId_ 已在类定义中声明为 std::atomic<uint32_t>）
void SeekCoordinator::seek(double time, bool isAccurate) noexcept {
    ++generationId_;   // 先递增，使所有旧包/帧立即失效（最后一次覆盖语义）
    // ... 后续 6 步
}

uint32_t SeekCoordinator::currentGeneration() const noexcept {
    return generationId_.load(std::memory_order_acquire);
}
```

```cpp
// ClockSync::popVideoFrame() 中校验代际，同时处理同 PTS 多帧（multimap）
AVFrameRefPtr ClockSync::extractFrontFrame(
        Synchronized<std::multimap<int64_t, TaggedFrame>>::LockedRef& frames) noexcept {
    const uint32_t curGen = pipeline_.seekCoordinator_->currentGeneration();
    // 丢弃所有代际不匹配的帧
    while (!frames->empty()) {
        auto it = frames->begin();
        if (it->second.generationId != curGen) {
            frames->erase(it);
            continue;
        }
        auto frame = std::move(it->second.frame);
        frames->erase(it);
        return frame;
    }
    return nullptr;
}
```

**`dataQueue_` 代际覆盖**：demux 入队口（`DemuxerComponent` 写入 `dataQueue_` 时）校验当前代际，旧代际数据直接丢弃，不进入解码队列：

```cpp
// DemuxerComponent 写入 dataQueue_ 时
void DemuxerComponent::onPacket(DataPacket packet) noexcept {
    const uint32_t curGen = pipeline_.seekCoordinator_->currentGeneration();
    if (packet.generationId != curGen) return;  // 旧代际，丢弃
    pipeline_.dataQueue_.push(std::move(packet));
}
```

`DataPacket` 同样携带 `generationId`，由 `SeekCoordinator::seek()` 递增后，`DemuxerComponent` 在下一次 demux 时将新代际写入 packet。

**`SeekCompleted` 触发条件**：按实际轨道决定完成信号来源：

- **有视频轨**：`onVideoFrameRendered()` 中校验 `generationId` 一致后发出 `PipelineEvent::SeekCompleted`
- **纯音频流（无视频轨）**：`onAudioPacketConsumed()` 中校验 `generationId` 一致后发出，防止 seek 完成事件永远不触发
- 判断依据为 `info_.hasVideo`，在 `DemuxHeaderParsed` 后已确定，`SeekCoordinator` 持有该标志

---

### 11.3 `DecoderComponent::open()` 错误上下文

**风险**：当前回调仅返回 `bool success`，线上失败无法区分配置错误、资源错误、格式不支持。

**方案**：回调改为携带错误码和描述信息（类型定义已在 §四 统一）。

```cpp
enum class DecoderOpenError : uint8_t {
    None,
    UnsupportedFormat,   // 格式不支持
    ResourceExhausted,   // 硬件解码器资源不足
    ConfigInvalid,       // 配置参数错误
    Unknown,
};

struct DecoderOpenResult {
    bool             success;
    DecoderOpenError error;
    std::string      message;
};

// DecoderComponent::open 签名
void open(DecoderType type,
          const std::shared_ptr<DecoderConfig>& config,
          std::function<void(DecoderOpenResult)> onOpened) noexcept;
```

回调通过 `postToOwnerThread()` 投递到 `ownerThread_` 后执行，保证串行（见 §11.1 跨线程回调强制投递规则）。

---

### 11.4 `PipelineEvent` 两阶段语义（已在 §四 统一定义）

**风险**：原 `Prepared` 语义模糊，上层收到后可能触发 `start()`，而此时解码器未就绪，导致早启动问题。

**已统一方案**（见 §四）：
- `DemuxHeaderParsed`：demuxer 解析头完成，媒体信息可读，**不可调用 `start()`**
- `PipelineReady`：所有实际存在轨道的 decoder 就绪，可安全调用 `start()`

**单轨媒体死锁修复**：`checkAndFirePipelineReady()` 按 `info_.hasAudio` / `info_.hasVideo` 计算期望 decoder 数量，仅音频流只等音频 decoder，仅视频流只等视频 decoder，不会永远等待不存在的 decoder。
---

### 11.5 `ClockSync` 追帧上限保护

追帧上限保护已合并到 §3.3 `requestVideoFrame` 核心逻辑中（`maxDropPerVsync` + `maxSyncWorkTimeUs` 双重保护）。

```cpp
// ClockSync 配置
struct ClockSyncConfig {
    int32_t maxDropPerVsync   = 8;      // 单次 vsync 最大丢帧数
    int64_t maxSyncWorkTimeUs = 8000;   // 单次 vsync 最大工作时间（微秒，默认 8ms）
};
```

**可观测指标**（`ClockSyncMetrics`）：每秒丢帧数、追帧轮次、vsync 回调耗时 p50/p99。

---

### 11.6 背压策略：本地 vs 网络差异化设计

**核心问题**：本地文件和网络流的 IO 特性完全不同，使用同一套阈值会导致：
- 本地播放：`dataQueue_` 填充极快，高水位立即触发 pause，但 IO 几乎无延迟，pause/resume 频繁抖动
- 网络播放：带宽有限、延迟高，需要提前预缓冲，低水位触发 resume 后要等待足够数据才能流畅播放

**方案**：引入 `MediaSourceType` 枚举，`BackpressureConfig` 按来源类型预设不同参数，`BufferMonitor` 在 `build()` 阶段由 `PlayPipeline` 根据 `IStream` 类型注入对应配置。

#### 11.6.1 配置结构

```cpp
enum class MediaSourceType : uint8_t {
    Local,    // 本地文件（file://、asset://）
    Network,  // 网络流（http://、https://、rtmp://、hls 等）
};

struct BackpressureConfig {
    // 水位阈值（纯时长维度）
    double highWatermarkDuration = 0.0;  // 缓冲时长高水位，触发 pause（秒）
    double lowWatermarkDuration  = 0.0;  // 缓冲时长低水位，触发 resume（秒）

    // 硬上限：超过此时长直接丢弃最旧数据，防内存无限膨胀（秒）
    double hardCapDuration       = 0.0;

    // 起播预缓冲（仅网络有意义）
    double startBufferDuration   = 0.0;  // 首次 start() 前需要预缓冲的最小时长（秒）
    double rebufferDuration      = 0.0;  // 卡顿后恢复播放所需的最小时长（秒）

    // 背压最小驻留时间：防止水位边界附近频繁 pause/resume 翻转
    int32_t minPauseHoldMs       = 300;  // pause 后至少持续此时长才允许 resume
    int32_t minResumeHoldMs      = 300;  // resume 后至少持续此时长才允许再次 pause

    // 直播/无界流标志：启用备用 PipelineReady 条件（首关键帧+音频包到达）
    bool isLiveSource            = false;

    /// 配置合法性校验：防止参数失配导致"尚未 Ready 就触发 pause"等问题
    /// 约束：startBufferDuration < highWatermarkDuration，低水位 < 高水位，硬上限 > 高水位
    [[nodiscard]] bool isValid() const noexcept {
        if (lowWatermarkDuration >= highWatermarkDuration) return false;
        if (hardCapDuration > 0.0 && hardCapDuration <= highWatermarkDuration) return false;
        if (startBufferDuration >= highWatermarkDuration) return false;  // 防止预缓冲未完成就触发 pause
        if (rebufferDuration > 0.0 && rebufferDuration >= highWatermarkDuration) return false;
        return true;
    }

    static BackpressureConfig forLocal() noexcept {
        BackpressureConfig c;
        // 本地 IO 极快，高水位设高避免频繁 pause，低水位设低快速 resume
        c.highWatermarkDuration = 30.0;
        c.lowWatermarkDuration  = 5.0;
        c.hardCapDuration       = 60.0;
        c.startBufferDuration   = 0.0;   // 本地无需预缓冲
        c.rebufferDuration      = 0.0;
        c.minPauseHoldMs        = 300;
        c.minResumeHoldMs       = 300;
        return c;
    }

    static BackpressureConfig forNetwork() noexcept {
        BackpressureConfig c;
        // 网络带宽有限，高水位适中防内存膨胀，低水位留足余量避免二次卡顿
        c.highWatermarkDuration = 10.0;
        c.lowWatermarkDuration  = 3.0;
        c.hardCapDuration       = 20.0;
        c.startBufferDuration   = 2.0;   // 首播预缓冲 2s
        c.rebufferDuration      = 1.5;   // 卡顿恢复需 1.5s
        c.minPauseHoldMs        = 500;
        c.minResumeHoldMs       = 500;
        return c;
    }

    static BackpressureConfig forLive() noexcept {
        BackpressureConfig c;
        // 直播：低延迟优先，高水位低，无起播预缓冲时长要求，备用 ready 条件启用
        c.highWatermarkDuration = 5.0;
        c.lowWatermarkDuration  = 1.0;
        c.hardCapDuration       = 10.0;
        c.startBufferDuration   = 1.0;
        c.rebufferDuration      = 1.0;
        c.minPauseHoldMs        = 200;
        c.minResumeHoldMs       = 200;
        c.isLiveSource          = true;  // 启用备用 ready 条件
        return c;
    }
};
```

#### 11.6.2 本地播放策略

本地文件 IO 带宽远超解码消耗，背压核心目标是**防止内存无限膨胀**，而非控制读取速度：

- **高水位设高**（30s）：减少 pause/resume 频率，避免抖动
- **无预缓冲**：`startBufferDuration = 0`，收到 `PipelineReady` 后立即可 `start()`
- **pause 后恢复极快**：低水位触发 resume 后，IO 几乎立即补满，不会二次卡顿
- **背压检查**：事件驱动 + 100ms 节流

```
本地背压状态机：
  [Flowing] ──dur >= 30s──→ [Paused]
  [Paused]  ──dur <= 5s ──→ [Flowing]
```

#### 11.6.3 网络播放策略

网络流存在带宽波动、延迟抖动、卡顿恢复等问题，背压需要兼顾**防膨胀**和**保流畅**两个目标：

- **起播预缓冲**：`DemuxHeaderParsed` 后不立即触发 `PipelineReady`，等待 `demuxedDuration() >= startBufferDuration`（2s）后才发出，避免起播即卡顿
- **卡顿检测**：`ClockSync` 连续 N 帧（默认 N=5）无可用帧时判定为卡顿，触发 `PipelineEvent::BufferingStart`；`BufferMonitor` 切换到 rebuffer 模式，等待时长 ≥ `rebufferDuration`（1.5s）后触发 `PipelineEvent::BufferingEnd` 恢复播放
- **低水位留余量**：低水位 3s，确保恢复播放后有足够缓冲应对网络抖动
- **背压检查**：事件驱动 + 50ms 节流（网络场景响应要更及时）

```
网络 Buffer 状态机：

  [Prebuffering] ──dur >= startBufferDuration──→ [Flowing]
       ↑                                              │
       │                                       dur >= 10s
       │                                              ↓
  [Rebuffering] ←──卡顿(连续N帧无帧)──────── [Paused]
       │                                              ↑
  dur >= rebufferDuration                      dur <= 3s
       │                                              │
       └──────────────────→ [Flowing] ───────────────┘
```

#### 11.6.4 统一的 `BufferMonitor` 实现

```cpp
void BufferMonitor::regulateDataFlow() noexcept {
    const auto duration = demuxedDuration();
    const auto now      = Clock::now();

    // 硬上限超限动作（顺序固定）：
    // 1. 先 pause（若未 pause）
    // 2. 再限速：丢弃最旧包直到降至 hardCap 以下
    // 3. 打点上报 BufferingStart，通知上层
    //
    // 线程安全约束：
    // - regulateDataFlow() 必须在 ownerThread_ 上执行（BufferMonitor 所有方法均如此）
    // - dataQueue_ 使用 Synchronized<> 包装，pop_front() 内部持锁；
    //   DemuxerComponent 的入队（push_back）同样通过 Synchronized<> 加锁，
    //   两者不会产生数据竞争，但 pop_front 循环期间 demux 线程仍可并发入队，
    //   因此循环条件每次重新调用 demuxedDuration()（持锁读取）而非缓存 duration
    if (duration > config_.hardCapDuration) {
        if (!isPaused_) {
            pipeline_.stream_->pause();
            isPaused_         = true;
            pauseRequestTime_ = now;
            pauseFailCount_   = 0;
        }
        while (demuxedDuration() > config_.hardCapDuration) {
            pipeline_.dataQueue_.pop_front();
        }
        postEventToOwnerThread(PipelineEvent::BufferingStart, std::nullopt);
        return;  // 本轮直接返回，等下次 tick 重新评估
    }

    // 正常水位控制，带最小驻留时间防止边界抖动
    const bool canPause  = !isPaused_
                        && (!lastResumeTime_.has_value()
                            || (now - *lastResumeTime_) >= std::chrono::milliseconds(config_.minResumeHoldMs));
    const bool canResume = isPaused_
                        && pauseRequestTime_.has_value()
                        && (now - *pauseRequestTime_) >= std::chrono::milliseconds(config_.minPauseHoldMs);

    if (duration >= config_.highWatermarkDuration && canPause) {
        pipeline_.stream_->pause();
        isPaused_         = true;
        pauseRequestTime_ = now;
        pauseFailCount_   = 0;
    } else if (duration <= config_.lowWatermarkDuration && canResume) {
        pipeline_.stream_->start();
        isPaused_        = false;
        lastResumeTime_  = now;
        pauseRequestTime_.reset();
    }

    // pause 失败兜底：分级处理，单次调度抖动不上报
    if (isPaused_ && pauseRequestTime_.has_value()) {
        const auto elapsed = now - *pauseRequestTime_;
        if (elapsed > std::chrono::milliseconds(500) && duration > config_.highWatermarkDuration) {
            ++pauseFailCount_;
            pauseRequestTime_ = now;
            if (pauseFailCount_ >= kPauseFailThreshold) {
                // 连续多次失败才升级为错误，携带完整上下文
                postEventToOwnerThread(PipelineEvent::ReadError,
                    PipelineError{PipelineEvent::ReadError,
                                  PipelineStage::BackPressure,
                                  -1,
                                  "stream pause not effective after retries"});
                pauseFailCount_ = 0;
            }
        }
    }

    checkRebufferComplete();
}

// 网络卡顿检测（由 ClockSync 在连续无帧时回调，仅网络模式生效）
void BufferMonitor::onStarvation(int32_t consecutiveMissCount) noexcept {
    if (sourceType_ != MediaSourceType::Network) return;
    if (consecutiveMissCount >= kStarvationThreshold && !isRebuffering_) {
        isRebuffering_ = true;
        postEventToOwnerThread(PipelineEvent::BufferingStart, std::nullopt);
    }
}

void BufferMonitor::checkRebufferComplete() noexcept {
    if (!isRebuffering_) return;
    if (demuxedDuration() >= config_.rebufferDuration) {
        isRebuffering_ = false;
        postEventToOwnerThread(PipelineEvent::BufferingEnd, std::nullopt);
    }
}

// 低频兜底心跳（200~500ms），仅做 resume 检查，不触发 pause
// 防止 pause 后入队事件变稀少导致 resume 判定延迟
void BufferMonitor::onHeartbeat() noexcept {
    if (!isPaused_) return;
    const auto duration = demuxedDuration();
    const auto now      = Clock::now();
    const bool canResume = pauseRequestTime_.has_value()
                        && (now - *pauseRequestTime_) >= std::chrono::milliseconds(config_.minPauseHoldMs);
    if (duration <= config_.lowWatermarkDuration && canResume) {
        pipeline_.stream_->start();
        isPaused_       = false;
        lastResumeTime_ = now;
        pauseRequestTime_.reset();
    }
}
```

#### 11.6.5 起播预缓冲与 `PipelineReady` 的关系

```
本地：DemuxHeaderParsed → checkAndFirePipelineReady() → PipelineReady（立即）
点播：DemuxHeaderParsed → 等待 dur >= startBufferDuration → PipelineReady（延迟）
直播：DemuxHeaderParsed → 等待 dur >= startBufferDuration
                         OR 首个关键帧 + 音频包均到达 → PipelineReady（备用条件）
```

`BufferMonitor` 在 `DemuxHeaderParsed` 后进入 `Prebuffering` 状态，持续监测 `demuxedDuration()`，达到阈值后调用 `checkAndFirePipelineReady()`。本地播放 `startBufferDuration = 0`，条件立即成立，行为与原来一致。

**直播/无界流备用 Ready 条件**：直播场景 `duration` 统计可能不稳定（窗口化、无 duration 字段），若 `demuxedDuration()` 长期无法达到阈值，备用条件按实际轨道存在性分支判断，避免误等不存在的轨道：

| 轨道组合     | 备用 Ready 触发条件                                     |
| ------------ | ------------------------------------------------------- |
| 音视频均存在 | 首个视频关键帧（IDR）已入队 **且** 至少一个音频包已入队 |
| 仅视频轨     | 首个视频关键帧（IDR）已入队                             |
| 仅音频轨     | 至少一个音频包已入队                                    |

备用条件仅在 `config_.isLiveSource == true` 时生效，点播流不启用。轨道存在性由 `DemuxHeaderParsed` 阶段填充的 `MediaInfo` 决定，`BufferMonitor` 在进入 `Prebuffering` 状态时读取一次并缓存，后续不再重新查询。

#### 11.6.6 新增 `PipelineEvent`

| 事件                      | 触发时机                   | 上层响应建议           |
| ------------------------- | -------------------------- | ---------------------- |
| `BufferingStart`          | 网络卡顿 / 硬上限触发      | 显示 loading 动画      |
| `BufferingEnd`            | rebuffer 完成，可继续播放  | 隐藏 loading，恢复播放 |
| `PrebufferingProgress(f)` | 预缓冲进度 0.0~1.0（可选） | 更新起播进度 UI        |

**背压检查周期**：事件驱动 + 节流（本地 100ms，网络 50ms），替代原 500ms 定时轮询。

---

### 11.7 数据结构选择

**`videoFrames_` 类型统一为 `std::multimap<int64_t, TaggedFrame>`**：

- 使用 `multimap` 而非 `map`：同 PTS 帧（某些流中存在）不会互相覆盖，避免隐性丢帧
- B 帧重排：按 PTS 排序，`begin()` 即为最早应渲染帧
- 若确认无 B 帧（如 H.264 Baseline Profile），可降级为 `std::deque<TaggedFrame>`，缓存局部性更好；由 `DemuxerComponent` 在 `build()` 阶段根据编解码器信息决定

**其他队列**：`dataQueue_` / `audioPackets_` / `videoPackets_` 统一使用 `std::deque`，替换原 `std::list`，缓存局部性更好。

**决策原则**：性能压测后再定型，避免过早固定容器。

---

### 11.8 生命周期幂等与状态机契约

**`PlayPipeline` 状态机**：

```
Idle ──build()──→ Built ──start()──→ Playing
                                        │
                              pause() ←─┤─→ pause()
                              resume()──┘
                                        │
                              seek()────┤（任意播放态均可）
                                        │
                              teardown()┘──→ Idle（任意态均可）
                                        │
                         join 超时 ──────┴──→ Faulted（终态）
```

**`Faulted` 终态约束**：`teardown()` join 超时后进入 `Faulted`，`isolated_` 置 `true`。此后所有 API（`start/pause/seek/flush`）在 API 层统一检查 `isolated_`，直接返回 no-op 并通过 `onEvent` 上报 `PipelineError{ReadError, PipelineStage::Demux, kErrInstanceFaulted, "instance faulted"}`，禁止继续操作损坏实例。上层收到后应销毁并重建播放器。

**各 API 幂等与非法状态行为**：

| API           | 重复调用                                                                                                                                       | 非法状态                           |
| ------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------- |
| `build()`     | no-op（已 Built 则跳过）                                                                                                                       | —                                  |
| `start()`     | no-op（已 Playing）                                                                                                                            | Built 之前：排队等待 PipelineReady |
| `pause()`     | no-op（已 Paused）                                                                                                                             | Idle/Built：no-op                  |
| `flush()`     | 可重入，清空队列                                                                                                                               | 任意态均可                         |
| `seek()`      | 新 seek 覆盖旧 seek（last-write-wins，generationId 递增）；被覆盖的旧 seek 发出 `SeekTimeout`                                               | Idle：no-op                        |
| `seek()` 节流 | 连续 seek 间隔 < `seekDebounceMs`（默认 100ms）时合并，只执行最后一次；超过 `seekTimeoutMs`（默认 5s）仍未完成则发 `SeekTimeout` 并重置状态 | —                                  |
| `teardown()`  | no-op（已 Idle）                                                                                                                               | **任意态均可，快速收敛**           |

**`teardown()` 快速收敛约束**（禁止 detach，改为有界 join + 回调闸门）：

```
1. 设置 teardownFlag_（std::atomic<bool>），所有内部循环检查此标志提前退出
2. ClockSync::teardownFlag_ 置 true，vsync 回调立即返回 nullptr（回调闸门）
3. 向所有内部线程（demuxThread、decoderThread）发送停止信号
4. 有界 join：等待最多 2s；超时后不 detach，也不强杀（标准 C++ 无安全强杀语义），
   而是记录 fatal 日志并隔离当前实例（标记为不可用，禁止后续调用），
   由上层决策是否重建播放器（协作式停止 + 失败降级）
5. 所有跨线程回调在投递到 ownerThread_ 前先检查 teardownFlag_，
   若已置位则丢弃回调，不投递
```

```cpp
void PlayPipeline::teardown() noexcept {
    teardownFlag_.store(true, std::memory_order_release);
    clockSync_->notifyTeardown();   // 置 ClockSync::teardownFlag_

    // 协作式停止：发信号，各线程内部循环检查 teardownFlag_ 后自行退出
    demuxer_->stop();
    audioDecoder_->stop();
    videoDecoder_->stop();

    // 有界 join，不 detach，不强杀（标准 C++ 无安全强杀语义）
    constexpr auto kJoinTimeout = std::chrono::seconds(2);
    if (!demuxer_->joinWithTimeout(kJoinTimeout)) {
        // 协作式停止超时：记录 fatal，隔离实例，由上层决策是否重建
        LOG_FATAL("demuxer thread join timeout, instance isolated");
        isolated_.store(true, std::memory_order_release);
    }
    // ... 其他线程同理
}

// postToOwnerThread 内部检查闸门
void PlayPipeline::postToOwnerThread(std::function<void()> task) noexcept {
    if (teardownFlag_.load(std::memory_order_acquire)) return;  // 丢弃，防悬空
    ownerThread_->post(std::move(task));
}
```

---

## 十二、优先级调整（稳健版）

| 阶段     | 改动                                                                             | 说明                               |
| -------- | -------------------------------------------------------------------------------- | ---------------------------------- |
| **P0**   | `PlayPipeline` 引入 + 队列归属迁移 + `Impl` 瘦身                                 | 基础结构，其他一切依赖此           |
| **P0.5** | 并发模型文档化 + `ownerThread_` 串行执行器约束                                   | 防止后续并发 bug                   |
| **P0.5** | seek `generationId` 代际隔离（最后一次覆盖语义）                                 | 防止旧帧回流，seek 正确性基础      |
| **P0.5** | 生命周期幂等契约 + `teardown()` 有界 join + 回调闸门                             | 稳定性基础，任意态 teardown 无死锁 |
| **P1**   | `ClockSync` 替换 AV Sync                                                         | 追帧内聚，vsync 驱动               |
| **P1**   | `SeekCoordinator` 替换 `SeekHandler`                                             | seek 单一入口                      |
| **P1**   | `DecoderComponent::open()` 错误上下文                                            | 消除静默失败                       |
| **P1**   | `PipelineEvent` 两阶段（`DemuxHeaderParsed` / `PipelineReady`）+ 单轨 ready 修复 | 消除早启动问题，单轨媒体不死锁     |
| **P2**   | `IVideoRender` 接口精简 + 推帧路径废弃                                           | 平台兼容风险大，建议独立灰度       |
| **P2**   | `AudioRenderComponent::setRenderEndCallback`                                     | 消除外部轮询                       |
| **P2**   | `ClockSync` 追帧上限保护（`maxDropPerVsync` / `maxSyncWorkTimeUs`）              | 防止 vsync 超时                    |
| **P2**   | 背压差异化策略（本地 vs 网络）+ `MediaSourceType` + 起播预缓冲 + 卡顿 rebuffer   | 本地防抖动，网络防卡顿，行为差异大 |
| **P3**   | `videoFrames_` / `dataQueue_` 容器优化                                           | 性能压测后定型                     |

---

## 十三、验收标准

### 功能正确性

- 连续 seek（含高频 seek，> 5次/秒）不出现旧帧闪回（`generationId` 校验覆盖）
- 仅音频 / 仅视频 / 音视频均可稳定播放至结束，`RenderEnd` 事件正确触发
- seek 后首帧渲染时间 < 500ms（正常网络）
- `teardown()` 在任意状态（Idle / Playing / Paused / Seeking）调用，无死锁、无崩溃，2s 内收敛

### 稳定性

- 长播（> 2h）内存无持续增长（`videoFrames_` / `audioFrames_` 队列有界）
- 高频 seek 压测（1000次）无内存泄漏、无 crash
- 网络抖动场景（模拟 pause/resume `IStream`）背压触发后队列峰值可控，恢复无振荡
- 本地播放：`demuxedDuration()` 峰值不超过 `hardCapDuration`（60s），pause/resume 频率 < 1次/秒
- 网络播放：卡顿后 `BufferingEnd` 触发前 `demuxedDuration()` ≥ `rebufferDuration`，不出现二次卡顿

### 性能

- vsync 回调（`ClockSync::requestVideoFrame()`）耗时 p99 < 2ms
- 单次 vsync 最大丢帧数不超过 `maxDropPerVsync`（默认 8 帧）
- 背压触发后 `demuxedDuration()` 峰值不超过 `hardCapDuration`

### 可观测性

- `DecoderComponent::open()` 失败时，上层可获取 `DecoderOpenError` 枚举 + 错误描述
- seek 完成事件携带实际 seek 到的时间戳（与请求时间的误差）
- `ClockSyncMetrics` 可查询：每秒丢帧数、追帧轮次、vsync 回调耗时分位数
- 关键生命周期事件（build/start/pause/seek/teardown）有日志可追踪
- 网络播放：`BufferingStart` / `BufferingEnd` 事件可追踪，卡顿次数与时长可统计
- `PrebufferingProgress` 事件可选上报，起播预缓冲进度可观测
