# HLS 缓存与数据流背压契约

## 目标

保证 HLS 数据源不会在播放、prebuffer 或首帧完成前无界下载，同时保持缓存通知和播放状态语义清晰。

## 时间语义

`notifyCacheTime` 上报绝对缓存终点，不是剩余缓存时长：

```text
cacheEndTime = 已进入 demux/pipeline 的媒体时间轴终点
forwardDuration = cacheEndTime - playbackTime
```

例如播放时间为 `6s`、缓存终点为 `84.56s`：

```text
notifyCacheTime = 84.56s
forwardDuration = 78.56s
```

背压使用 `forwardDuration` 与水位比较，不能裁剪 `cacheEndTime` 后再通知外部。

## 缓存职责拆分

播放缓存水位由两个对象分担，输出完全不同因此不再耦合在一个对象里：

**`SourceFlowController`**（消费 `SourceBackpressureConfig`）— 控制生产者：

- `source.highWatermarkSec`：达到后暂停数据源；
- `source.lowWatermarkSec`：降到后恢复数据源，并通知容量可用；
- 高低水位之间是滞回区，保持当前状态；
- `pauseForOperation/resumeForOperation` 用于 seek 等操作级暂停，与水位滞回相互独立；
- `reset()` 必须清 `operationPaused_`，否则下一次 `interruptStream()` 被短路。

**`BufferingTracker`**（消费 `BufferingConfig`）— 上报就绪/卡顿：

- `buffering.startBufferSec`：达到后触发 `PipelineReady`；
- `buffering.starvationThreshold`：饥饿计数达到后触发 `BufferingStart`；
- `buffering.rebufferTargetSec() = startBufferSec × rebufferScale`：进入卡顿的阈值（严格小于才进入）；
- `buffering.rebufferExitSec() = rebufferTargetSec() × rebufferScale`：退出卡顿的阈值（达到即退出）；两者之间是有死区的滞回，避免队列在阈值附近抖动时反复上报 `BufferingStart/BufferingEnd`；
- prebuffer 阶段不触发 `BufferingStart/BufferingEnd`；
- `readyFired_` 只表示启动缓存完成，不是背压开关。

两者由 `PlayPipeline` 用**同一个 `demuxedDuration()` 快照**驱动，避免两次采样结果不一致而自相矛盾：

```text
onDataEnqueued()               <- demux 数据入队事件（防抖 + post 到 owner 线程）
updateBufferControl()          <- 100ms 节流（live 50ms）
runBufferControl()             <- onFastTick，不节流
    -> duration = demuxedDuration()
    -> sourceFlow_->update(duration)
    -> bufferingTracker_->update(duration, allowBufferingEvents)
```

聚合层约束：`buffering.rebufferExitSec() < source.highWatermarkSec`。生产在高水位停止，因此退出所需的缓存水平必须低于高水位，否则卡顿永不退出。

## HLS 生产边界

只在 demux 输出侧暂停是不可靠的。HLS segment 完成后会立即调度下一个 segment；如果等待 demux 结果或下一次 heartbeat 才暂停，source 可能已经完成全部 VOD segment。

因此 `HLSStream::downloadSegments()` 是下一分片的唯一生产闸门：

```text
downloadSegments()
    -> 检查 productionGate
    -> 不允许：不发起下一 segment 请求
    -> 允许：downloadSegment()
```

`productionGate` 使用 pipeline 当前前向缓存：

```text
forwardDuration < source.highWatermarkSec
    -> 允许下载下一 segment

forwardDuration >= source.highWatermarkSec
    -> 不启动下一 segment
```

闸门只负责防止继续生产，不新增独立的缓存状态机。`SourceFlowController` 仍负责在高低水位变化时调用 source `pause/resume`：

```text
SourceFlowController interruptStream()
    -> HLSStream::pause()
    -> 当前 segment 收尾后不再启动下一 segment

SourceFlowController resumeStream()
    -> HLSStream::start()
    -> HLSStream::downloadSegments()
    -> productionGate 允许后继续下载
```

**已知问题**：`HLSStream::pause()` 是破坏性的（`controlSession_->close()` + 取消全部定时器），而 `productionGate` 只是"不发起下一片"（连接在途、定时器保留）。两者都由同一个 `demuxedDuration` 驱动，但代价差一个数量级。水位背压应当只走 `pause()` 这条路径中不破坏连接的部分，或统一收敛到生产闸门；目前 HTTP / 本地文件没有轻量闸门，只能依赖 `pause()`。

允许一个已经在途的 segment 完成，因此缓存可能略高于高水位，但不应再无界增长到几十秒。`HLSStream` 保留一个待提交 chunk，是因为 HTTP 流的最终事件可能不携带数据，必须等 `isFinal` 才能正确标记上一块 `isLastChunk`；这不是额外的背压状态。

## 播放状态边界

`Playing`、`Paused`、`Buffering` 只控制消费侧：

- 音频消费和时钟；
- VSync 拉帧；
- 视频渲染。

播放状态路径不得无条件启动数据源，绕过 `SourceFlowController` 的挂起状态。数据源恢复只来自：

- `SourceFlowController` 低水位 `openFetchGate()`；
- seek 完成后的显式 `resumeStream()`；
- source 初始化。

seek 可以使用已有 source pause/resume，但不能永久关闭背压，也不能引入没有恢复路径的全局绕过开关。

## 水位挂起与操作暂停的区分

两条路径语义不同，不可混用：

| 路径 | 触发 | 手段 | 语义 |
|---|---|---|---|
| 水位（背压） | 高/低水位 | `closeFetchGate()` / `openFetchGate()` | 允许在途生产完成，只停止发起新的生产单元 |
| 操作 | seek / 切轨 / 停止 | `interruptStream()` / `resumeStream()` | 立即中断，丢弃在途数据 |

关键约束：

- 水位路径**不得**调用 `interruptStream()`/`resumeStream()`。对 HLS 而言 `pause()` 是破坏性的（`controlSession_->close()` + 取消包括 playlist refresh 在内的全部定时器），且恢复时 `++generation_` 会让 pause 前已下载未 demux 的数据被 demuxer 全部丢弃，形成"下载—丢弃—重下"循环。
- 低水位采样**不得**释放操作级暂停，否则 seek 过程中源会被水位误恢复。
- `closeFetchGate()` 是幂等的边沿触发；`openFetchGate()` 可以重复发。

## 生产闸门（统一接口）

所有源通过 `IStream` 的同一套闸门接口被调节，不再按类型分派：

```text
setProductionGate(gate)   生产者自己的生产单元边界上同步查询
openFetchGate()           水位恢复时唤醒因闸门关闭而挂起的生产者
closeFetchGate()          无生产单元边界的源由此直接停止（边沿触发）
```

| 源 | 生产单元边界 | 闸门方式 |
|---|---|---|
| `FileStream` | 每个 read chunk（64KB） | **pull**：`readLoop()` 在每个 chunk 前查询，关闭时 park |
| `HLSStream` | 每个 segment | **pull**：`downloadSegments()` 查询 |
| `HttpStream` | 无（一次性大 Range 请求） | **push**：`closeFetchGate()` → `pause()`，`openFetchGate()` → `start()` |

闸门查询统一走 `SourceFlowController::isFetchAllowed()`（即 `PlayPipeline::canFetchMore()`）：
它的值就是唯一的滞回状态，**不是**对 `highWatermarkSec` 的第二次比较 —— 死区对所有源类型都生效。

水位关闭对 pull 型源是空操作：它们会在下一个生产单元边界自行停下，天然满足"允许在途生产完成"。

已知限制：`HttpStream` 无法在请求中途停止，水位超限仍是 `pause()` + 靠 `deliveredPosition_` 续传，代价是一次连接重建。要支持"允许在途完成"需先把请求分片化。

`MediaSourceManager` 只做转发，不再有 `dynamic_cast<HLSStream*>` 分支。

## 生命周期

```text
创建 pipeline / SourceFlowController / BufferingTracker
        |
        v
绑定 HLS productionGate
        |
        v
source start
        |
        v
prebuffer
  - 执行高低水位背压
  - 达到 buffering.startBufferSec 后触发 PipelineReady
        |
        v
首帧提交 / 首帧渲染
  - 只负责首帧和渲染状态
  - 不决定是否启用背压
        |
        v
steady flow
  - SourceFlowController 继续按 high/low watermark 控制 source
  - BufferingTracker 按 rebufferTargetSec 上报 BufferingStart/End
```

## 回归测试契约

至少保证：

1. 超过高水位会调用 `closeFetchGate()`，且**不会**调用 `interruptStream()`；
2. prebuffer 阶段不会触发 `BufferingStart/BufferingEnd`；
3. 低水位会调用 `openFetchGate()`，且**不会**调用 `resumeStream()`；
4. HLS production gate 关闭时不会发起下一 segment 请求；
5. production gate 恢复后能够继续下载并最终完成；
6. `notifyCacheTime` 保持绝对缓存终点语义；
7. seek 不会永久关闭后续背压；
8. 已在途 segment 允许收尾，但不会启动新的无界 segment 链；
9. 操作级暂停不会被低水位采样释放；
10. `reset()` 只丢弃水位挂起状态，不触碰 delegate；操作暂停必须由 `resumeForOperation()` 配对释放（否则下一次 `pauseForOperation()` 被短路，源永不恢复）。

## 统一滞回原语

所有背压点共用 `src/core/pipeline/WatermarkGate.h`：

- `shouldCloseGate(current, low, high, closed)` —— 全局唯一的滞回判定定义；
- `WatermarkGate<Level>` —— 水位路径 `update()` 与操作路径 `forceClose()/forceOpen()` 两条独立入口。

实例化点：Q1 源闸门 `WatermarkGate<double>`；Q3 解码器喂料 `WatermarkGate<int32_t>`。

相关实现和测试：

- `src/core/HLSStream.h`
- `src/core/HLSStream.cpp`
- `src/core/MediaSourceManager.h`
- `src/core/MediaSourceManager.cpp`
- `src/core/PlayerImpl.cpp`
- `src/core/pipeline/SourceFlowController.h`
- `src/core/pipeline/SourceFlowController.cpp`
- `src/core/pipeline/BufferingTracker.h`
- `src/core/pipeline/BufferingTracker.cpp`
- `include/FlowConfig.h`
- `src/core/pipeline/FlowConfigDefaults.h`
- `src/core/pipeline/PlayPipeline.h`
- `src/core/pipeline/PlayPipeline.cpp`
- `test/core/HLSStreamTest.cpp`
- `test/core/SourceFlowControllerTest.cpp`
- `test/core/BufferingTrackerTest.cpp`
- `test/core/CorePipelineComponentTest.cpp`
