# IStream 架构概览

## 接口定义

`IStream`（`src/base/IStream.h`）是所有数据流的抽象基类，定义了统一的 IO 接口。

### 核心接口

| 接口                         | 说明                                                     |
| ---------------------------- | -------------------------------------------------------- |
| `open(OpenParams)`           | 打开流，返回 `StreamInfo`                                |
| `start()`                    | 开始读取数据                                             |
| `pause()`                    | 暂停读取                                                 |
| `close()`                    | 关闭流并释放资源                                         |
| `seek(uint64_t byteOffset)`  | 基于字节偏移的 seek（File/HTTP 使用）                    |
| `seekByTime(double timeSec)` | 基于时间的 seek（HLS 使用），默认返回 `InvalidOperation` |
| `updateRange(Range)`         | 更新读取范围                                             |

### seek 语义分离

不同流类型的 seek 语义不同，通过两个接口明确区分：

- **`seek(uint64_t byteOffset)`**：字节偏移 seek
  - `FileStream`：直接 fseek 到指定偏移
  - `HttpStream`：取消当前请求，重新发起带 `Range: bytes=N-` 的 HTTP 请求
  - `HLSStream`：返回 `InvalidOperation`（HLS 不支持字节 seek）

- **`seekByTime(double timeSec)`**：时间 seek
  - `HLSStream`：通过 `M3U8Playlist::findSegmentByTime` 定位目标 segment，取消当前下载，重新拉取目标 segment
  - 其他实现：返回 `InvalidOperation`（默认实现）

## 实现类

### FileStream
- 本地文件流，`seek` 直接调用 `fseek`
- `isSeekable = true`，`isLive = false`

### HttpStream（`src/core/HttpStream.h`）
- HTTP 渐进式下载流
- `open` 时乐观预设 `isSeekable = true`
- `seek` 取消当前请求并重新发起 Range 请求
- 通过响应头 `Accept-Ranges: bytes` 确认服务端支持 Range

### HLSStream（`src/core/HLSStream.h`）
- HLS 分片流，支持 Master Playlist 和 Media Playlist
- `seek(uint64_t)` 返回 `InvalidOperation`
- `seekByTime(double)` 定位 segment index，重新下载目标 segment
- 直播流（`isLive = true`）的 `seekByTime` 返回 `InvalidOperation`

## 数据流向

```
MediaSourceManager
    └── IStream (File/Http/HLS)
            └── OpenParams::onData → DataPacket → Channel<DataPacket> → DemuxerComponent
```

## 相关变更记录

- **2026-06**：引入 `seekByTime(double)` 虚接口，将 HLS 时间 seek 与 HTTP/File 字节 seek 语义分离；`HLSStream::seek(uint64_t)` 改为返回 `InvalidOperation`
