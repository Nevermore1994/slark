## IStream 架构介绍

### 文档目标

本文档介绍 Slark 流读取抽象 `IStream` 的接口设计、实现映射、运行流程和生命周期协作方式。内容以“架构现状说明”为主，不包含设计评判。

---

## 模块定位

`IStream` 位于播放器数据入口层，负责把不同来源（本地文件、HTTP、HLS）的数据统一抽象为可消费的分片回调。

在整体链路中的位置：

- 上游：URI/路径输入、网络请求或文件系统
- 中间：`IStream` 及其实现（`FileStream`、`HttpStream`、`HLSStream`）
- 下游：`Player::Impl` 的数据回调，转入 `DataPacket` 队列，再进入 `DemuxerComponent`

---

## 核心抽象：`IStream`

文件：[IStream.hpp](/Users/nevermore-/selfspace/slark/src/base/IStream.hpp)

### 1) 类型体系

- `StreamType`：`File` / `Http` / `HLS`
- `StreamState`：`Idle` / `Opening` / `Ready` / `Reading` / `Paused` / `Completed` / `Error`
- `StreamError`：统一错误码与错误信息
- `StreamResult<T>`：基于 `std::expected` 的返回结果封装

### 2) 数据承载

- `DataChunk`
  - `data`：当前分片视图
  - `owner`：分片底层数据所有权
  - `offset`：全局偏移
  - `segmentIndex`：分段序号（HLS 场景使用）
  - `isLastChunk`：是否最后一个分片

- `StreamInfo`
  - `type`、`totalSize`、`duration`
  - `isSeekable`、`isLive`
  - `contentType`

### 3) 回调与参数

- `OpenParams`
  - `uri`
  - `range`
  - `readChunkSize`
  - `onData` / `onMetadata` / `onError` / `onComplete`

### 4) 生命周期接口

- `open(const OpenParams&) -> StreamResult<StreamInfo>`
- `start()` / `pause()` / `close()`
- `seek(uint64_t position) -> StreamResult<void>`
- `updateRange(Range range)`
- 查询接口：`state()` / `info()` / `position()` / `isRunning()` / `isCompleted()`

`IStream` 对上层暴露统一行为，对下层屏蔽传输介质差异。

---

## 实现层职责划分

### 1) 本地文件流：`FileStream`

文件：
- [FileStream.hpp](/Users/nevermore-/selfspace/slark/src/base/FileStream.hpp)
- [FileStream.cpp](/Users/nevermore-/selfspace/slark/src/base/FileStream.cpp)

职责：

- 使用 `File::ReadFile` 打开本地文件
- 可选按 `Range` 定位初始读取区间
- 在独立线程 `readLoop()` 中按 `readChunkSize` 连续读数据
- 每次读取后通过 `onData(DataChunk)` 向上游推送
- EOF 或 range 结束后触发 `onComplete`

状态特征：

- `open()` 后进入 `Ready`
- `start()` 后进入 `Reading`
- `pause()` 进入 `Paused`
- 读取结束进入 `Completed`
- `close()` 回到 `Idle`

### 2) HTTP 流：`HttpStream`

文件：
- [HttpStream.hpp](/Users/nevermore-/selfspace/slark/src/core/HttpStream.hpp)
- [HttpStream.cpp](/Users/nevermore-/selfspace/slark/src/core/HttpStream.cpp)

职责：

- 基于 `http::HttpRequest` 执行流式下载
- 从响应头提取 `Accept-Ranges`、`Content-Length`、`Content-Range`
- 通过 `handleChunk()` 把网络分片转为 `DataChunk`
- 网络错误时转为 `StreamError::NetworkError`

seek 行为：

- 仅在服务端支持 `Range` 时可 seek
- seek 会取消当前请求并重新发起新范围请求

### 3) HLS 流：`HLSStream`

文件：
- [HLSStream.hpp](/Users/nevermore-/selfspace/slark/src/core/HLSStream.hpp)
- [HLSStream.cpp](/Users/nevermore-/selfspace/slark/src/core/HLSStream.cpp)

职责：

- 拉取并解析 master/media playlist
- 构建 `MediaPlaylist`（variant + segment）
- 按 segment 顺序下载 TS 分片并通过 `onData` 推送数据
- 通过 `segmentIndex` 标记分片来源
- playlist 就绪后通过 metadata 回调上送播放列表信息

seek 行为：

- 输入位置按毫秒解释
- 通过时间映射到目标 segment，并更新 `currentSegmentIndex_`

---

## 工厂与实现选择

文件：
- [StreamFactory.hpp](/Users/nevermore-/selfspace/slark/src/core/StreamFactory.hpp)
- [StreamFactory.cpp](/Users/nevermore-/selfspace/slark/src/core/StreamFactory.cpp)

`StreamFactory` 负责把 URI 映射为具体流实现：

- `.m3u8` / `.m3u` → `HLSStream`
- 本地路径或 `file://` → `FileStream`
- `http://` / `https://` → `HttpStream`

输出统一为 `std::shared_ptr<IStream>`，上层只依赖接口。

---

## 在播放器中的接入链路

### 1) 创建与打开

入口文件：
- [MediaSourceManager.h](/Users/nevermore-/selfspace/slark/src/core/MediaSourceManager.h)
- [PlayerImpl.cpp](/Users/nevermore-/selfspace/slark/src/core/PlayerImpl.cpp)

流程：

1. `Player::Impl::setupDataProvider()` 调用 `mediaSourceManager_.open(uri, onData, onError, onComplete)`。
2. `MediaSourceManager::open()` 调用 `StreamFactory::createStream(uri)` 创建具体流实现。
3. 组装 `OpenParams`，在 `onData` 回调中完成 `DataChunk` → `DataPacket` 转换。
4. 执行 `stream_->open(params)`，成功后缓存 `StreamInfo`。

### 2) 数据流转

1. `mediaSourceManager_.start()` 开始读取。
2. `onData(DataChunk)` 回调在 `MediaSourceManager::open` 内部转换为 `DataPacket`。
3. `DataPacket` 直接推送给 `DemuxerComponent::pushData()`。

### 3) 错误与完成

- `onError`：转换为播放器错误事件（网络或文件错误）
- `onComplete`：记录读取完成状态（后续由播放链路决定结束态）

---

## 生命周期与状态协同

`IStream` 与播放器状态协作关系：

- 初始化阶段：`open()` → `Ready`
- 播放阶段：`start()` → `Reading`
- 缓冲/限速阶段：可调用 `pause()` 暂停输入
- seek 阶段：`seek()` 或 `updateRange()` 调整读取起点/范围
- 停止阶段：`close()` 回收资源并回到 `Idle`

这套协同使“数据输入速率控制”和“解封装/解码速率控制”可以在不同层分离处理。

---

## 关键结构摘要

```text
Player::Impl
  └─ MediaSourceManager::open(uri)
      └─ StreamFactory::createStream(uri)
          ├─ FileStream
          ├─ HttpStream
          └─ HLSStream

IStream::onData(DataChunk)
  └─ MediaSourceManager 内部转换为 DataPacket
      └─ DemuxerComponent::pushData
```

---

## 一句话总结

`IStream` 架构通过“统一接口 + 工厂分发 + 回调驱动”把本地文件、HTTP 和 HLS 三类输入源收敛为一致的数据分片模型，向播放器后续 demux/解码链路提供统一、可控的输入通道。