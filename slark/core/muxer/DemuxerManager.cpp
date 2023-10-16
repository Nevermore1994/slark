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
        { WAVDemuxer::info().symbol, WAVDemuxer::info() }
    };
}

bool DemuxerManager::contains(DemuxerType type) const noexcept {
    for (const auto& pair : demuxers_) {
        if (pair.second.type == type) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<IDemuxer> DemuxerManager::create(DemuxerType type) noexcept {
    if (!contains(type)) {
        return nullptr;
    }
    std::unique_ptr<IDemuxer> p = nullptr;
    switch (type) {
        case DemuxerType::WAV:
            return createDemuxer<WAVDemuxer>(WAVDemuxer::info().demuxerName);
        default:
            p = nullptr;
            break;
    }
    return p;
}

DemuxerType DemuxerManager::probeDemuxType(const std::string& str) const noexcept {
    return probeDemuxType(std::string_view(str));
}

DemuxerType DemuxerManager::probeDemuxType(const std::string_view& str) const noexcept {
    for (const auto& pair : demuxers_) {
        if (auto pos = str.find(pair.first); pos != std::string::npos) {
            return pair.second.type;
        }
    }
    return DemuxerType::Unknown;
}

}//end namespace slark
