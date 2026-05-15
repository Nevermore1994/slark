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

    [[nodiscard]] virtual bool isRunning() const noexcept = 0;
};

} // namespace slark
