# DemuxerComponent 架构概览

## 职责

`DemuxerComponent`（`src/core/demuxer/DemuxerComponent.h`）是 demux 层的核心组件，负责：
1. 从 `Channel<DataPacket>` 接收 IO 层推送的数据
2. 探测并创建合适的 `IDemuxer` 实例
3. 驱动 demuxer 解析数据，将结果通过回调分发给 `PlayPipeline`
4. 在 seek 时通知 IO 层重新定位

## 线程模型

- `worker_`（`DemuxerWorker` 线程）：独占 `runLoop`，阻塞在 `receiver_->receive()`
- 主调线程（ownerThread）：调用 `flush()`、`seekToPos()`、`close()` 等控制接口

## seek 链路

```
PlayPipeline::seek()
    └── DemuxerComponent::seekToPos(pos)
            ├── demuxer->seekPos(pos)          // 更新 demuxer 内部读取位置
            └── invokeSeekFunc(Range(pos, 0))  // 非 HLS：通知 IO 层重新定位
                    └── PlayerImpl::setHandleSeekFunc 注册的回调
                            ├── demuxerComponent_->flush()
                            └── mediaSourceManager_.seek(pos)  // 重新发起 Range 请求
```

**HLS 特殊处理**：`seekToPos` 检测到 `DemuxerType::HLS` 时跳过 `invokeSeekFunc`，HLS seek 由 `PlayPipeline::seek()` 直接调用 `seekByTimeFunc_` 完成。

## flush 机制

`flush()` 由主调线程发起：
1. 设置 `flushed_` 原子标志
2. 清空 Channel 残留 packet（`tryReceiveAll`）
3. 调用 `demuxer->flush()`
4. 唤醒 worker（`receiver_->wakeup()`）

worker 在 `runLoop` 顶部检测 `flushed_` 并再次清空，确保 seek 后不处理旧数据。

## 回调接口

| 回调                | 类型                    | 触发时机                 |
| ------------------- | ----------------------- | ------------------------ |
| `handleResultFunc_` | `HandleDemuxResultFunc` | 每次 `parseData` 完成后  |
| `handleSeekFunc_`   | `HandleSeekFunc`        | `seekToPos` 时（非 HLS） |

## 相关变更记录

- **2026-06**：`seekToPos()` 在非 HLS demuxer 时触发 `invokeSeekFunc`，打通 seek → IO 层重定位链路；HLS 通过 `PlayPipeline::seekByTimeFunc_` 走时间 seek 路径
