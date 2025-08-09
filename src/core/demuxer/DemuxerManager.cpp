//
//  DemuxerManager.cpp
//  slark
//
//  Created by Nevermore on 2022/5/2.
//

#include "DemuxerManager.h"
#include "WavDemuxer.h"
#include "Mp4Demuxer.h"
#include "HLSDemuxer.h"

namespace slark {

DemuxerManager& DemuxerManager::shareInstance()
{
   static DemuxerManager instance;
   return instance;
}

DemuxerManager::DemuxerManager() {
    init();
}

void DemuxerManager::init() noexcept {
    demuxers_ = {
        { WAVDemuxer::info().type, WAVDemuxer::info() },
        { Mp4Demuxer::info().type, Mp4Demuxer::info() },
        { HLSDemuxer::info().type, HLSDemuxer::info() },
    };
}

bool DemuxerManager::contains(DemuxerType type) const noexcept {
    return demuxers_.contains(type);
}

std::shared_ptr<IDemuxer> DemuxerManager::create(DemuxerType type) const noexcept {
    if (!contains(type)) {
        return nullptr;
    }
    auto& demuxerInfo = demuxers_.at(type);
    return BaseClass::create<IDemuxer>(demuxerInfo.demuxerName);
}

DemuxerType DemuxerManager::probeDemuxType(DataView str) const noexcept {
    for (const auto& pair : demuxers_) {
        if (auto pos = str.find(pair.second.symbol); pos != std::string::npos) {
            return pair.second.type;
        }
    }
    return DemuxerType::Unknown;
}

}//end namespace slark
