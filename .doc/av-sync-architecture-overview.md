## 音视频同步架构介绍

### 文档目标

本文档介绍 Slark 播放内核中音视频同步（AV Sync）的设计原理、当前实现现状，以及经过讨论确定的重构方向。

---

## 模块定位

音视频同步负责协调视频帧的渲染时机，使视频画面与音频播放保持一致。其核心问题是：**在每次 vsync 回调时，决定当前应该渲染哪一帧，还是跳过本次渲染**。

---

## 核心概念

### 时钟基准

Slark 以**音频时钟**为主时钟（master clock）。音频渲染由系统音频引擎驱动，时间精度高且连续，因此以 `audioRenderTime` 作为参考基准，视频向音频对齐。

### 同步差值

```
diff = audioTime - videoTime
```

- `diff > 0`：视频落后于音频（视频慢了）
- `diff < 0`：视频超前于音频（视频快了）
- `diff ≈ 0`：音视频同步

### 同步动作（AVSyncAction）

| 动作     | 触发条件                                  | 行为                     |
| -------- | ----------------------------------------- | ------------------------ |
| `Render` | `abs(diff) < 0.04s` 或 `abs(diff) >= 10s` | 正常渲染当前帧           |
| `Hold`   | `diff < -0.04s`（视频超前）               | 跳过本次渲染，等音频追上 |
| `Drop`   | `diff > 0.04s`（视频落后）                | 追赶音频                 |

`kAVSyncMinThreshold = 0.04s`（40ms，覆盖人眼对音视频不同步的感知阈值），`kAVSyncMaxThreshold = 10.0s`（超过 10s 认为时钟异常，强制渲染）。

### 音频主时钟（audioMasterTime）

`requestVideoFrame()` 中的 diff 计算使用 `audioMasterTime()`，而非 `clockSync_->audioTime()`：

- **有音频时**：使用 `audioRender_->playedTime()`，即 `AudioTrack.getTimestamp()` 返回的硬件实际播出时间（µs 级精度），消除音频缓冲区延迟（50~100ms）带来的系统性偏差
- **无音频时**：回退到 `clockSync_->audioTime()`（视频时钟自驱）

`clockSync_->setAudioTime()` 仍在 `requestAudioFrame()` 中更新，用于 `demuxedDuration()` 背压水位计算，语义不变。

---

## 当前实现（PipelineScheduler）

主要文件：

- [PipelineScheduler.hpp](/Users/nevermore-/selfspace/slark/src/core/PipelineScheduler.hpp)
- [PipelineScheduler.cc](/Users/nevermore-/selfspace/slark/src/core/PipelineScheduler.cc)

当前 AV Sync 逻辑内嵌在 `PipelineScheduler` 中，存在两条视频帧消费路径：推帧路径（`pushVideoFrameToRender()`，ownerThread 10ms 驱动）和拉帧路径（`requestRender()`，vsync 驱动）并存。拉帧路径在 `Drop` 时采用“回退时间戳”的方式绕过追帧，逻辑不直观，且依赖外部定时器的多次触发才能追上。

---

## 设计方向（重构目标）
经过讨论，确定以下设计方向：

### 核心原则

1. **追帧逻辑内聚在 AV Sync 内部**，由 vsync 驱动，不依赖 `ownerThread_` 的 10ms 轮询频率。
2. **不丢帧**：视频落后时通过连续消费帧追赶，中间帧直接丢弃（不渲染），而非跳过解码。
3. **低帧率视频自然不丢帧**：视频帧率低于 vsync 频率时，帧队列中没有积压帧，追赶循环自然退出。

### 视频帧率与 vsync 频率的关系

| 场景                                               | 追赶策略                                                        |
| -------------------------------------------------- | --------------------------------------------------------------- |
| 视频帧率 < vsync 频率（如 30fps 视频，60fps 屏幕） | 有空闲 vsync，每次 vsync 多消费一帧即可追上，不丢帧             |
| 视频帧率 ≥ vsync 频率（如 60fps 视频，60fps 屏幕） | 无空闲 vsync，必须在单次 vsync 内消费多帧（丢弃中间帧）才能追上 |

低帧率场景下，`while` 追赶循环因帧队列为空而自然退出，无需显式区分两种场景，逻辑统一。

### 重构后的 `requestVideoFrame` 设计

```cpp
AVFrameRefPtr ClockSync::requestVideoFrame() noexcept {
    double diff = audioTime() - videoTime();
    if (isAudioRenderEnd_) diff = 0;

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
    // 低帧率视频：队列里没有多余帧，循环立即退出，不丢帧
    // 高帧率视频（fps >= vsync）：队列里有积压帧，丢弃中间帧追赶
    diff = audioTime() - videoTime();
    while (evaluateAVSync(diff) == AVSyncAction::Drop) {
        auto nextFrame = popVideoFrame();
        if (!nextFrame) break;
        frame = std::move(nextFrame);
        setVideoTime(frame->ptsTime());
        diff = audioTime() - videoTime();
    }

    return frame;
}
```

### 整体视频帧消费路径（重构后）

```
videoFrames_（已解码帧缓冲）
      │
      ├── 拉：vsync → requestVideoFrame()
      │         正常播放，VideoRender 自驱
      │         视频落后时在内部连续消费帧追赶
      │
      └── 推：forceRenderFirstFrame()
              seek 完成后强制渲染第一帧
```

---

## 与周边模块的关系

- [PipelineScheduler.hpp](/Users/nevermore-/selfspace/slark/src/core/PipelineScheduler.hpp)
  - 当前承载 AV sync 逻辑，重构后 AV sync 职责将独立到专属模块
- [PlayerImpl.h](/Users/nevermore-/selfspace/slark/src/core/PlayerImpl.h)
  - 持有音视频时钟（`audioRenderTime` / `videoRenderTime`）
- [Clock.h](/Users/nevermore-/selfspace/slark/src/base/Clock.h)
  - 底层时钟实现，支持暂停、速率调整

---

## 一句话总结

音视频同步以音频时钟为基准，通过 `diff = audioTime - videoTime` 决定视频帧的渲染时机；重构方向是将追帧逻辑内聚到 vsync 驱动的拉帧路径中，利用帧率差异自然处理丢帧与不丢帧两种场景，消除对外部定时器频率的依赖。
