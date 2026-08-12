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

## BufferMonitor 职责

`BufferMonitor` 是唯一的播放缓存水位状态机：

- `demuxHighWatermarkDuration`：达到后暂停数据源；
- `demuxLowWatermarkDuration`：降到后恢复数据源；
- `startBufferDuration`：达到后触发 `PipelineReady`；
- prebuffer 阶段也执行数据流高低水位控制；
- prebuffer 阶段不触发 `BufferingStart/BufferingEnd`；
- `readyFired_` 只表示启动缓存完成，不是背压开关。

同一次检查使用一个 `demuxedDuration` 快照：

```text
onDemuxedDataEnqueued / onHeartbeat
    -> duration = demuxedDuration()
    -> regulateDataFlow(duration, allowBufferingEvents)
    -> 如果处于 prebuffer，再检查 startBufferDuration
```

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
forwardDuration < demuxHighWatermarkDuration
    -> 允许下载下一 segment

forwardDuration >= demuxHighWatermarkDuration
    -> 不启动下一 segment
```

闸门只负责防止继续生产，不新增独立的缓存状态机。现有 `BufferMonitor` 仍负责在高低水位变化时调用 source `pause/resume`：

```text
BufferMonitor pauseStream()
    -> HLSStream::pause()
    -> 当前 segment 收尾后不再启动下一 segment

BufferMonitor resumeStream()
    -> HLSStream::start()
    -> HLSStream::downloadSegments()
    -> productionGate 允许后继续下载
```

允许一个已经在途的 segment 完成，因此缓存可能略高于高水位，但不应再无界增长到几十秒。`HLSStream` 保留一个待提交 chunk，是因为 HTTP 流的最终事件可能不携带数据，必须等 `isFinal` 才能正确标记上一块 `isLastChunk`；这不是额外的背压状态。

## 播放状态边界

`Playing`、`Paused`、`Buffering` 只控制消费侧：

- 音频消费和时钟；
- VSync 拉帧；
- 视频渲染。

播放状态路径不得无条件启动数据源，绕过 `BufferMonitor` 的暂停状态。数据源恢复只来自：

- `BufferMonitor` 低水位恢复；
- seek 完成后的显式恢复；
- source 初始化。

seek 可以使用已有 source pause/resume，但不能永久关闭背压，也不能引入没有恢复路径的全局绕过开关。

## 生命周期

```text
创建 pipeline / BufferMonitor
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
  - 达到 startBufferDuration 后触发 PipelineReady
        |
        v
首帧提交 / 首帧渲染
  - 只负责首帧和渲染状态
  - 不决定是否启用背压
        |
        v
steady flow
  - BufferMonitor 继续按 high/low watermark 控制 source
```

## 回归测试契约

至少保证：

1. prebuffer 阶段超过高水位会调用 `pauseStream()`；
2. prebuffer 阶段不会触发 `BufferingStart/BufferingEnd`；
3. 低水位会恢复已暂停的数据源；
4. HLS production gate 关闭时不会发起下一 segment 请求；
5. production gate 恢复后能够继续下载并最终完成；
6. `notifyCacheTime` 保持绝对缓存终点语义；
7. seek 不会永久关闭后续背压；
8. 已在途 segment 允许收尾，但不会启动新的无界 segment 链。

相关实现和测试：

- `src/core/HLSStream.h`
- `src/core/HLSStream.cpp`
- `src/core/MediaSourceManager.h`
- `src/core/MediaSourceManager.cpp`
- `src/core/pipeline/BufferMonitor.h`
- `src/core/pipeline/BufferMonitor.cpp`
- `src/core/pipeline/PlayPipeline.h`
- `src/core/pipeline/PlayPipeline.cpp`
- `test/core/HLSStreamTest.cpp`
- `test/core/BufferMonitorTest.cpp`
