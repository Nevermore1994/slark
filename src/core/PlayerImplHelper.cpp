//
//  PlayerImplHelper.cpp
//  slark
//
//  Created by Nevermore on 2024/12/25.
//

#include "PlayerImplHelper.h"
#include "PlayerImpl.h"
#include "Mp4Demuxer.h"
#include "HLSDemuxer.h"
#include "HLSReader.h"
#include "MediaBase.h"
#include <regex>

namespace slark {

PlayerImplHelper::PlayerImplHelper(std::weak_ptr<Player::Impl> player)
    : player_(player) {
    
}

bool PlayerImplHelper::createDataProvider(const std::string& path, void* ptr, ReaderDataCallBack callback) noexcept  {
    if (path.empty()) {
        return false;
    }
    auto impl = reinterpret_cast<Player::Impl*>(ptr);
    if (isHlsLink(path)) {
        auto reader = std::make_unique<HLSReader>();
        auto demuxer = std::make_shared<HLSDemuxer>();
        reader->setDemuxer(demuxer);
        impl->demuxer_ = demuxer;
        impl->dataProvider_ = std::move(reader);
    } else if (isNetworkLink(path)) {
        impl->dataProvider_ = std::make_unique<RemoteReader>();
    } else {
        impl->dataProvider_ = std::make_unique<Reader>();
    }
    ReaderTaskPtr task = std::make_unique<ReaderTask>(std::move(callback));
    task->path = path;
    task->callBack = std::move(callback);
    if (!impl->dataProvider_->open(std::move(task))) {
        LogE("data provider open error!");
    }
    return true;
}

bool PlayerImplHelper::openDemuxer() noexcept {
    auto player = player_.lock();
    if (!player) {
        LogE("player is nullptr.");
        return false;
    }
    auto res = player->demuxer_->open(probeBuffer_);
    auto demuxerType = player->demuxer_->type();
    if (demuxerType == DemuxerType::MP4) {
        handleOpenMp4DemuxerResult(res);
    }
    if (res) {
        resetProbeData();
    }
    return res;
}

void PlayerImplHelper::handleOpenMp4DemuxerResult(bool isSuccess) noexcept {
    auto player = player_.lock();
    if (!player) {
        LogE("player is nullptr.");
        return;
    }
    auto mp4Demuxer = std::dynamic_pointer_cast<Mp4Demuxer>(player->demuxer_);
    if (isSuccess) {
        auto dataStart = mp4Demuxer->headerInfo()->headerLength + 8; //skip size and type
        Range range;
        range.pos = dataStart;
        range.size = static_cast<int64_t>(mp4Demuxer->headerInfo()->dataSize);
        mp4Demuxer->seekPos(dataStart);
        player->dataProvider_->updateReadRange(range);
        LogI("mp4 seek to:{}", dataStart);
//#if DEBUG
//            auto ss = mp4Demuxer->description();
//            LogI("mp4 info:{}", ss);
//#endif
    } else {
        int64_t moovBoxStart = 0;
        uint32_t moovBoxSize = 0;
        constexpr int64_t kMb = 1024 * 1024;
        if (mp4Demuxer->probeMoovBox(*probeBuffer_, moovBoxStart, moovBoxSize)) {
            return;
        }
        auto pos = static_cast<int64_t>(probeBuffer_->pos());
        if (pos == 0) {
            auto headerInfo = mp4Demuxer->headerInfo();
            if (headerInfo) {
                pos = static_cast<int64_t>(headerInfo->headerLength + headerInfo->dataSize);
            } else {
                pos = static_cast<int64_t>(player->dataProvider_->size()) - kMb;
            }
        } else {
            pos -= kMb;
        }
        if (pos < 0) {
            return;
        }
        auto probePos = static_cast<uint64_t>(pos);
        player->demuxer_->seekPos(probePos);
        player->dataProvider_->seek(probePos);
        probeBuffer_->reset();
        probeBuffer_->setOffset(probePos);
    }
}

bool PlayerImplHelper::appendProbeData(uint64_t offset, DataPtr ptr) noexcept {
    if (probeBuffer_ == nullptr) {
        probeBuffer_ = std::make_unique<Buffer>();
    }
    if (!probeBuffer_->append(offset, std::move(ptr))) {
        LogE("append data error:{}", offset);
        return false;
    }
    return true;
}

void PlayerImplHelper::resetProbeData() noexcept {
    probeBuffer_.reset();
}

DataView PlayerImplHelper::probeView() const noexcept {
    return probeBuffer_->view();
}

}
