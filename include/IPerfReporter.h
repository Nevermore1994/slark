//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "PerfTypes.h"

#include <memory>

namespace slark::perf {

struct FrameTrace;

struct IPerfReporter {
    /**
     * @brief 帧跟踪完成回调
     * @param trace 帧跟踪只读快照
     */
    virtual void onFrameTrace(std::shared_ptr<const FrameTrace> trace) = 0;

    /**
     * @brief 播放会话结束回调
     * @param report 会话完整上报数据，含首帧耗时、阶段统计、资源统计等（只读，禁止修改）
     */
    virtual void onSessionComplete(std::shared_ptr<const SessionReport> report) = 0;

    virtual ~IPerfReporter() = default;
};

} // namespace slark::perf
namespace slark::perf
