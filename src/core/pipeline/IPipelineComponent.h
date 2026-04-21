//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

namespace slark {

/**
 * @brief 管线组件统一生命周期接口。
 *
 * 所有管线组件（Demuxer/Decoder/AudioRender/VideoRender）实现此接口，
 * PlayPipeline 通过 components_ 容器统一管理组件的 start/pause/close/flush，
 * 消除逐组件 if-check 的重复模式。
 */
class IPipelineComponent {
public:
    virtual ~IPipelineComponent() = default;

    virtual void start() noexcept = 0;
    virtual void pause() noexcept = 0;

    virtual void close() noexcept = 0;
    virtual void flush() noexcept = 0;

    // seek 时暂停流输入输出，由需要特殊处理的组件 override
    virtual void seekStream() noexcept {}

    [[nodiscard]] virtual bool isRunning() const noexcept = 0;
};

} // namespace slark
namespace slark
