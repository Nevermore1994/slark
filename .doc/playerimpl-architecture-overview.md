# PlayerImpl 架构概览

## 职责

`Player::Impl`（`src/core/PlayerImpl.h`）是播放器核心控制层，负责：
- 状态机管理（`PlayerStateMachine`）
- 事件分发（ownerThread 上的事件循环）
- 协调 `MediaSourceManager`、`DemuxerComponent`、`PlayPipeline` 三层

## 线程模型

- `ownerThread_`（`playerThread`）：事件循环、状态机、pipeline tick
- IO 线程（HTTP coroutine）：数据回调、错误回调
- DemuxerWorker：demux 循环

所有状态变更通过 `setState()` 完成，内部持有 `RWLock` 保护。

## seek 完整链路

```
Player::seek(time)
    └── sender_->send(EventType::Seek)
            └── ownerThread: doSeek(seekRequest)
                    ├── setState(Buffering)
                    └── pipeline_->seek(CTime(time))
                            ├── [FastForwardInGOP] 直接快进，不触发 IO seek
                            ├── [HLS] seekByTimeFunc_(timeSec)
                            │       └── mediaSourceManager_.seekByTime(timeSec)
                            │               └── HLSStream::seekByTime → 切换 segment
                            └── [File/HTTP] demuxer_->seekToPos(pos)
                                    ├── demuxer->seekPos(pos)
                                    └── invokeSeekFunc → handleSeekFunc 回调
                                            ├── demuxerComponent_->flush()
                                            └── mediaSourceManager_.seek(pos)
                                                    └── HttpStream::seek → 重新发 Range 请求
```

## 错误处理

### IO 错误（网络断流等）
```
HttpStream/HLSStream::onError
    └── PlayerImpl::onError lambda（IO 线程）
            ├── notifyPlayerEvent(OnError, errorCode)
            └── setState(Error) → doStop()
```

### seek 失败
```
mediaSourceManager_.seek() 返回 unexpected
    └── notifyPlayerEvent(OnError)
    └── setState(Error)
```

### seek 超时
```
PlayPipeline::onHeartbeat()
    ├── 本地源：2000ms 超时
    ├── 网络源：8000ms 超时
    └── 超时后：recoverFromSeekTimeout() + fireEvent(SeekTimeout)
            └── PlayerImpl::handlePipelineEvent(SeekTimeout)
                    └── resumeFromBuffering()
```

## 状态机

```
NotInited → Initializing → Prepared → Buffering ⇄ Playing
                                           ↓
                                         Pause
                                           ↓
                                    Completed / Error / Stop
```

## 关键组件生命周期

| 组件                        | 创建时机                   | 销毁时机                |
| --------------------------- | -------------------------- | ----------------------- |
| `MediaSourceManager`        | `setupDataProvider()`      | `doStop()`              |
| `DemuxerComponent`          | `createDemuxerComponent()` | `doStop()`              |
| `PlayPipeline`              | `buildPipeline()`          | `doStop()`              |
| `AudioDecoder/VideoDecoder` | `preparePlayerInfo()`      | `pipeline_->teardown()` |

## 相关变更记录

- **2026-06**：
  - `onError` 回调去重，直接调用 `setState(Error)` 进入错误状态，不再发送 `EventType::ReadError` 事件
  - `setHandleSeekFunc` 回调感知 `seek()` 失败，立即上报错误
  - `buildPipeline` 注入 `seekByTimeFunc_` 和 `streamType`，支持 HLS 时间 seek 路径
