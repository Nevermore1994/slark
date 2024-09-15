//
//  DemuxerManager.cpp
//  slark
//
//  Created by Nevermore on 2022/5/2.
//

#include "DemuxerManager.h"
#include "WavDemuxer.h"

namespace slark {

DemuxerManager::DemuxerManager() {
    init();
}

void DemuxerManager::init() noexcept {
    demuxers_ = {
        { WAVDemuxer::info().type, WAVDemuxer::info() }
    };
}

bool DemuxerManager::contains(DemuxerType type) const noexcept {
    return demuxers_.contains(type);
}

std::unique_ptr<IDemuxer> DemuxerManager::create(DemuxerType type) const noexcept {
    if (!contains(type)) {
        return nullptr;
    }
    auto& demuxerInfo = demuxers_.at(type);
    return std::unique_ptr<IDemuxer>(BaseClass<IDemuxer>::create(demuxerInfo.demuxerName));
}

DemuxerType DemuxerManager::probeDemuxType(std::string_view str) const noexcept {
    for (const auto& pair : demuxers_) {
        if (auto pos = str.find(pair.second.symbol); pos != std::string::npos) {
            return pair.second.type;
        }
    }
    return DemuxerType::Unknown;
}

}//end namespace slark
