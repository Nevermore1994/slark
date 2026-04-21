//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Buffer.hpp"
#include "NonCopyable.h"
#include "Range.h"

#include <string>
#include <vector>

namespace slark {

struct TSInfo {
    bool isDiscontinuity = false;
    bool isSupportByteRange = false;
    uint32_t sequence = 0;
    double duration = 0.0;
    double startTime = 0.0;
    std::string url;
    Range range;

    [[nodiscard]] double endTime() const noexcept {
        return startTime + duration;
    }
};

struct PlayListInfo {
    uint32_t codeRate = 0;
    std::string m3u8Url;
    std::string name;
};

class M3U8Parser : public NonCopyMovable {
public:
    explicit M3U8Parser(std::string baseUrl);
    ~M3U8Parser() = default;

    void reset() noexcept;
    bool parse(Buffer& buffer) noexcept;

    [[nodiscard]] const std::vector<PlayListInfo>& playListInfos() const noexcept {
        return playListInfos_;
    }

    [[nodiscard]] const std::vector<TSInfo>& tsInfos() const noexcept {
        return infos_;
    }

    [[nodiscard]] bool isPlayList() const noexcept {
        return isPlayList_;
    }

    [[nodiscard]] bool isCompleted() const noexcept {
        return isCompleted_;
    }

    [[nodiscard]] double totalDuration() const noexcept {
        return totalDuration_;
    }

private:
    bool isPlayList_ = false;
    bool isCompleted_ = false;
    uint32_t index_ = 0;
    double totalDuration_ = 0.0;
    std::string baseUrl_;
    std::vector<TSInfo> infos_;
    std::vector<PlayListInfo> playListInfos_;
};

} // namespace slark
namespace slark
