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
#include "MediaUtil.h"
#include <regex>
#include <utility>

namespace slark {

PlayerImplHelper::PlayerImplHelper(std::weak_ptr<Player::Impl> player)
    : player_(std::move(player)) {
    
}

bool PlayerImplHelper::createDataProvider(
    const std::string& path,
    ReaderDataCallBack callback
) noexcept  {
    auto impl = player_.lock();
    if (path.empty() || !impl) {
        return false;
    }
    if (isHlsLink(path)) {
        auto reader = std::make_unique<HLSReader>();
        auto demuxer = DemuxerManager::shareInstance().create(DemuxerType::HLS);
        impl->createDemuxerComponent();
        reader->setDemuxer(std::dynamic_pointer_cast<HLSDemuxer>(demuxer));
        impl->demuxerComponent_->setDemuxer(std::move(demuxer));
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


bool PlayerImplHelper::isRenderEnd() noexcept {
    auto player = player_.lock();
    if (!player) {
        return true;
    }
    if (player->stats_.isRenderEnd()) {
        LogI("isRenderEnd");
        return true;
    }
    constexpr double kMaxDriftTime = 0.5; //second
    auto videoRenderTime = player->videoRenderTime();
    auto audioRenderTime = player->audioRenderTime();
    if (isEqualOrGreater(std::min(audioRenderTime, videoRenderTime),
                         player->info_.duration + kMaxDriftTime)) {
        LogI("render end, video time:{}, audio time:{}, duration:{}",
                videoRenderTime, audioRenderTime, player->info_.duration);
        return true;
    }
    return false;
}

bool PlayerImplHelper::seekToLastAvailableKeyframe(
    double targetTime
) noexcept {
    auto player = player_.lock();
    if (!player) {
        return false;
    }
    constexpr int64_t kInvalid = -1;
    int64_t lastKeyFramePts = kInvalid;
    double lastKeyFrameTime = 0.0;
    player->videoPackets_.withLock([targetTime, &lastKeyFramePts, &lastKeyFrameTime](auto& packets) {
        for (auto& packet: packets | std::views::reverse) {
            auto ptsTime = packet->ptsTime();
            if (ptsTime < targetTime) {
                packet->isDiscard = true;
            }
            if (auto videoFrameInfo = std::dynamic_pointer_cast<VideoFrameInfo>(packet->info)) {
                if (videoFrameInfo->isIDRFrame && ptsTime <= targetTime) {
                    lastKeyFramePts = packet->pts;
                    lastKeyFrameTime = ptsTime;
                    break;
                }
            }
        }

        if (lastKeyFramePts != kInvalid) {
            std::erase_if(packets, [&lastKeyFramePts](auto& frame) {
                return frame->pts < lastKeyFramePts;
            });
        }
    });

    LogI("key frame pts time:{}", lastKeyFrameTime);
    return lastKeyFramePts != kInvalid;
}

}
