//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "IDemuxer.h"
#include "NonCopyable.h"

#include <memory>
#include <unordered_set>

namespace slark {

class DemuxerManager : public slark::NonCopyMovable {
public:
    static DemuxerManager& shareInstance();

    DemuxerManager();

    ~DemuxerManager() = default;

public:
    void init() noexcept;

    bool contains(DemuxerType type) const noexcept;

    DemuxerType probeDemuxType(DataView str) const noexcept;

    std::shared_ptr<IDemuxer> create(DemuxerType type) const noexcept;

private:
    std::unordered_map<DemuxerType, DemuxerInfo> demuxers_;
};

} // namespace slark

/* DemuxerManager_hpp */
