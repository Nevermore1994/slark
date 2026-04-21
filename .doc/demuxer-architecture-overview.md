## demuxer 架构介绍

### 文档目标

本文档用于介绍 Slark 播放内核中 `src/core/demuxer` 模块的架构组成、职责边界与运行流程。内容以“现状说明”为主，不包含设计优劣评判。

### 模块在播放器中的位置

`demuxer` 位于播放器主链路中间层，位于数据源读取与解码器之间。

- 上游：`IStream` 实现（本地文件流、HTTP 流、HLS 流）
- 中间：`DemuxerComponent` + `IDemuxer` 具体实现（`Mp4Demuxer`、`HLSDemuxer`、`WAVDemuxer`）
- 下游：`PacketHandler`、`DecoderComponent`（音频/视频解码）

在运行时，`Player::Impl` 会持续把上游读取到的 `DataPacket` 推入 `DemuxerComponent`，再由 demux 结果回调到播放器管线。

---

## 分层与核心对象

### 1) Demuxer 抽象层：`IDemuxer`

文件：`src/core/demuxer/IDemuxer.hpp`

`IDemuxer` 定义统一解封装契约，核心能力包括：

- `open(std::unique_ptr<Buffer>&)`：基于 probe 缓冲进行打开/头部解析
- `onOpened()`：返回 `OpenResult`，指导组件层执行后处理（如 seek、重排 probe 数据）
- `parseData(DataPacket&)`：增量解析输入数据，输出 `DemuxerResult`
- `seekPos(uint64_t)` / `getSeekToPos(double)`：seek 相关能力
- `close()` / `reset()`：生命周期管理

同时，`IDemuxer` 提供通用状态与媒体信息承载：

- `isOpened_`、`isCompleted_`
- `videoInfo_`、`audioInfo_`、`headerInfo_`
- `totalDuration_`

`DemuxerResult` 由 `DemuxerResultCode` + 音视频帧数组组成，用于统一向上游反馈解析结果。

### 2) 管理与工厂层：`DemuxerManager`

文件：`src/core/demuxer/DemuxerManager.hpp`、`src/core/demuxer/DemuxerManager.cc`

`DemuxerManager` 是类型探测与实例创建入口：

- `probeDemuxType(DataView)`：按固定顺序探测容器类型（当前包含 MP4/HLS/WAV）
- `create(DemuxerType)`：通过反射工厂按类型创建具体 demuxer

每个 demuxer 通过 `SLARK_DEMUXER_INFO(...)` 注册自身探测信息（magic symbol、offset、类型）。

### 3) 组件编排层：`DemuxerComponent`

文件：`src/core/demuxer/DemuxerComponent.hpp`、`src/core/demuxer/DemuxerComponent.cc`

`DemuxerComponent` 负责把抽象 demuxer 接入播放器运行时：

- 维护输入队列：`dataList_`
- 持有探测缓冲：`probeBuffer_`
- 通过工作线程周期执行 `demuxData()`
- 在首次阶段创建并打开 demuxer，打开后进入持续解析阶段
- 通过两个回调向上层交互：
  - `HandleDemuxResultFunc`：输出解析结果
  - `HandleSeekFunc`：通知上层调整数据源读取区间

组件层本身不关心具体封装格式细节，只负责驱动生命周期和数据调度。

---

## 具体 demuxer 实现

### 1) MP4：`Mp4Demuxer`

文件：`src/core/demuxer/Mp4Demuxer.hpp`、`src/core/demuxer/Mp4Demuxer.cpp`

`Mp4Demuxer` 的工作由两部分组成：

- 头部与 box 树解析：
  - 解析 `moov` / `mdat` 以及轨道相关 box（`stsd/stts/stsc/stsz/stco/ctts/stss` 等）
  - 提取音视频元信息（编码类型、分辨率、采样率、SPS/PPS/VPS 等）
- 增量解析媒体数据：
  - 基于 `TrackContext` 维护轨道读取游标
  - 按数据到达情况在音视频轨道间选择可解析的最早样本
  - 产出 `AVFrame` 队列

`TrackContext` 是 MP4 中的关键内部对象，封装了样本索引推进、seek 对齐、时间戳计算等轨道级逻辑。

### 2) HLS/TS：`HLSDemuxer` + `TSDemuxer`

文件：`src/core/demuxer/HLSDemuxer.hpp`、`src/core/demuxer/HLSDemuxer.cpp`

`HLSDemuxer` 作为 HLS 入口，主要职责：

- 在 `open()` 阶段直接进入可用状态（playlist 解析由上层流模块负责）
- 持有 `TSDemuxer` 处理 MPEG-TS 包解析
- 处理 segment seek（通过 TS index 协同）

`TSDemuxer` 负责 TS 细节：

- TS 包同步与拆包（188 字节）
- PAT/PMT 解析，确定音视频 PID
- PES 聚合与 PTS/DTS 提取
- H264 NALU 分段并打包视频帧
- ADTS 解析并打包 AAC 音频帧

平台适配点位于视频写包路径：

- Android：写入 Annex-B 起始码头
- iOS：写入长度前缀头

### 3) WAV：`WAVDemuxer`

文件：`src/core/demuxer/WavDemuxer.hpp`、`src/core/demuxer/WavDemuxer.cpp`

`WAVDemuxer` 面向 RIFF/WAVE 音频容器：

- 在 `open()` 中解析 `RIFF/WAVE`、`fmt `、`data` 等 chunk
- 提取声道数、采样率、位深、时长与头长度
- 在 `parseData()` 中按固定采样窗口切分音频帧并输出

---

## HLS playlist 相关子模块

### `M3U8Parser`

文件：`src/core/demuxer/M3U8Parser.hpp`、`src/core/demuxer/M3U8Parser.cc`

`M3U8Parser` 提供 m3u8 文本解析能力：

- 解析媒体分片信息：`#EXTINF`、`#EXT-X-BYTERANGE`、`#EXT-X-DISCONTINUITY`、URL
- 解析多码率信息：`#EXT-X-STREAM-INF`
- 输出：
  - `TSInfo` 列表（分片时长、起始时间、URL、range 等）
  - `PlayListInfo` 列表（码率、子 m3u8 URL）

该解析器处于 demux 相关目录，但在实际播放链路中，HLS playlist 的主流程由 `HLSStream` 侧管理。

---

## 从播放器到 demux 的调用链

### 入口与创建

1. `Player::Impl::setupDataProvider()` 创建数据源并注册 `onData` 回调。
2. `ComponentFactory::createDataProvider()` 根据 URI 创建流对象。
3. 对 HLS 流，工厂会预创建 `DemuxerComponent` 并显式设置 `HLSDemuxer`。
4. 非 HLS 情况下，`DemuxerComponent` 会在收到数据后通过探测自动创建具体 demuxer。

### 数据投递与解析

1. 数据源回调把 chunk 转成 `DataPacket`，进入 `Player::Impl::dataList_`。
2. `Player::Impl::demuxData()` 将累积数据批量推送给 `DemuxerComponent::pushData()`。
3. `DemuxerComponent` 工作线程周期执行：
   - 未打开：probe + `DemuxerManager` 探测 + `open()/onOpened()`
   - 已打开：`parseData()` 增量解析
4. 解析结果通过 `HandleDemuxResultFunc` 回传上层。

### 结果消费

- `ParsedHeader`：播放器进入 prepared 流程，创建解码组件
- 音视频帧：进入 `PacketHandler`，后续进入解码/渲染链路
- `HandleSeekFunc`：触发上游数据源调整读取区间

---

## 生命周期与状态协作

### `DemuxerComponent` 侧

- `start()`：启动 demux 工作线程
- `pause()`：暂停 demux 线程
- `reset()`：关闭当前 demuxer、清空队列和 probe 数据
- `close()`：在 reset 基础上标记组件关闭
- `flush()`：设置冲刷标记，下一轮清理内部输入队列

### `IDemuxer` 侧

- `open()` + `onOpened()`：建立解析上下文
- `seekPos()`：切换解析位置并重置局部状态
- `parseData()`：持续消费增量数据
- `close()/reset()`：回到初始状态

播放器在 `play/pause/seek/stop` 流程中通过组件层与 demux 生命周期协作。

---

## seek 语义说明

`getSeekToPos(double time)` 的返回值语义按容器类型不同：

- HLS：返回 TS segment index 语义（由 segment 级定位配合）
- MP4/WAV：返回文件字节偏移语义

`DemuxerComponent::seekToPos(uint64_t)` 统一接收该位置并转发给当前 demuxer，同时通过回调通知上游数据源更新读取范围。

---

## 目录结构（demuxer）

```text
src/core/demuxer/
├── IDemuxer.hpp
├── DemuxerManager.hpp
├── DemuxerManager.cc
├── DemuxerComponent.hpp
├── DemuxerComponent.cc
├── Mp4Box.hpp
├── Mp4Box.cpp
├── Mp4Demuxer.hpp
├── Mp4Demuxer.cpp
├── HLSDemuxer.hpp
├── HLSDemuxer.cpp
├── M3U8Parser.hpp
├── M3U8Parser.cc
├── WavDemuxer.hpp
└── WavDemuxer.cpp
```

---

## 一句话总结

当前 demuxer 架构可概括为：`DemuxerComponent` 负责运行时编排，`IDemuxer` 统一格式契约，`DemuxerManager` 负责探测与创建，`MP4/HLS/WAV` 实现各自容器解析，并通过统一结果结构向播放器后续解码链路输出音视频帧。