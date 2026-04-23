//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

namespace slark {

class IPipelineComponent {
public:
    virtual ~IPipelineComponent() = default;

    virtual void start() noexcept = 0;
    virtual void pause() noexcept = 0;

    virtual void close() noexcept = 0;
    virtual void flush() noexcept = 0;

    // seek 时暂停流输入输出，由需要特殊处理的组件 override
    virtual void seekStream() noexcept = 0;

    [[nodiscard]] virtual bool isRunning() const noexcept = 0;
};

} // namespace slark
