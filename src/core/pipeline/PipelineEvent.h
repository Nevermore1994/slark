//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <cstdint>

namespace slark {

enum class PipelineEvent : uint8_t {
    DemuxHeaderParsed,  // demux 头部解析完成
    PipelineReady,      // 解码器就绪，可以开始渲染
    FirstFrameRendered, // 首帧已渲染，Buffering -> Ready
    AudioDecoderFailed, // 音频解码器打开失败（PipelineError 必填）
    VideoDecoderFailed, // 视频解码器打开失败（PipelineError 必填）
    BufferingStart,     // 开始缓冲
    BufferingEnd,       // 缓冲结束
    ReadError,          // 读取 / 背压错误（PipelineError 必填）
    SeekCompleted,      // seek 完成
    DecodeError,        // 连续解码失败超阈值（PipelineError 必填）
    RenderEnd,          // 播放结束
};

} // namespace slark
