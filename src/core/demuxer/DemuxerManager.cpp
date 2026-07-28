//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DemuxerManager.h"

#include "HLSDemuxer.h"
#include "Mp4Demuxer.h"
#include "Reflection.hpp"
#include "WavDemuxer.h"

namespace slark {

DemuxerManager& DemuxerManager::shareInstance() {
    static DemuxerManager instance;
    return instance;
}

DemuxerManager::DemuxerManager() {
    init();
}

void DemuxerManager::init() noexcept {
    demuxers_[Mp4Demuxer::info().type] = Mp4Demuxer::info();
    demuxers_[WAVDemuxer::info().type] = WAVDemuxer::info();
    demuxers_[HLSDemuxer::info().type] = HLSDemuxer::info();
}

bool DemuxerManager::contains(DemuxerType type) const noexcept {
    return demuxers_.contains(type);
}

DemuxerType DemuxerManager::probeDemuxType(DataView data) const noexcept {
    // 按固定顺序探测，避免 unordered_map 迭代顺序不稳定
    static const std::array<DemuxerType, 3> kProbeOrder = {
        DemuxerType::MP4,
        DemuxerType::HLS,
        DemuxerType::WAV,
    };
    for (auto type : kProbeOrder) {
        auto it = demuxers_.find(type);
        if (it == demuxers_.end()) {
            continue;
        }
        const auto& info = it->second;
        auto end = info.symbolOffset + info.symbol.size();
        if (data.size() < end) {
            continue;
        }
        auto view = DataView(data.data() + info.symbolOffset, info.symbol.size());
        if (std::equal(view.begin(), view.end(), info.symbol.begin())) {
            return type;
        }
    }
    return DemuxerType::Unknown;
}

std::shared_ptr<IDemuxer> DemuxerManager::create(DemuxerType type) const noexcept {
    auto it = demuxers_.find(type);
    if (it == demuxers_.end()) {
        return nullptr;
    }
    return Reflection::shareInstance().create<IDemuxer>(it->second.demuxerName);
}

}  // namespace slark
