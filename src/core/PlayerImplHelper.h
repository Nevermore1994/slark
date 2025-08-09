//
//  PlayerImplHelper.h
//  slark
//
//  Created by Nevermore on 2024/12/25.
//

#pragma once

#include "Reader.h"
#include "RemoteReader.h"
#include "Buffer.hpp"
#include "Player.h"
#include "AVFrame.hpp"
#include <map>

namespace slark {

struct PlayerDebugInfo {
    Time::TimePoint receiveTime;
    Time::TimePoint createdTime;
    Time::TimePoint createdDemuxerTime;
    Time::TimePoint openedDemuxerTime;
    Time::TimePoint createAudioDecoderTime;
    Time::TimePoint openedAudioDecoderTime;
    Time::TimePoint createAudioRenderTime;
    Time::TimePoint createVideoDecoderTime;
    Time::TimePoint openedVideoDecoderTime;
    Time::TimePoint pushVideoDecodeTime;
    Time::TimePoint pushVideoRenderTime;

    void reset() noexcept {
        receiveTime = 0;
        createdTime = 0;
        createdDemuxerTime = 0;
        openedDemuxerTime = 0;
        createAudioDecoderTime = 0;
        openedAudioDecoderTime = 0;
        createVideoDecoderTime = 0;
        openedVideoDecoderTime = 0;
        pushVideoDecodeTime = 0;
        pushVideoRenderTime = 0;
    }

    void printTimeDeltas() const noexcept {
        std::string debugInfo("=== Player Debug Time Deltas ===\n");

        if (createdTime != 0 && receiveTime != 0) {
            auto delta = createdTime - receiveTime;
            debugInfo.append(std::format("Player creation time: {}\n", delta.toMilliSeconds()));
        }

        if (createdDemuxerTime != 0 && receiveTime != 0) {
            auto delta = createdDemuxerTime - receiveTime;
            debugInfo.append(std::format("Demuxer creation time: {}\n", delta.toMilliSeconds()));
        }

        if (openedDemuxerTime != 0 && receiveTime != 0) {
            auto delta = openedDemuxerTime - receiveTime;
            debugInfo.append(std::format("Demuxer opening time: {}\n", delta.toMilliSeconds()));
        }

        if (createAudioDecoderTime != 0 && receiveTime != 0) {
            auto delta = createAudioDecoderTime - receiveTime;
            debugInfo.append(std::format("Audio decoder creation time: {}\n", delta.toMilliSeconds()));
        }

        if (openedAudioDecoderTime != 0 && receiveTime != 0) {
            auto delta = openedAudioDecoderTime - receiveTime;
            debugInfo.append(std::format("Audio decoder opening time: {}\n", delta.toMilliSeconds()));
        }

        if (createAudioRenderTime != 0 && receiveTime != 0) {
            auto delta = createAudioRenderTime - receiveTime;
            debugInfo.append(std::format("Audio render creation time: {}\n", delta.toMilliSeconds()));
        }

        if (createVideoDecoderTime != 0 && receiveTime != 0) {
            auto delta = createVideoDecoderTime - receiveTime;
            debugInfo.append(std::format("Video decoder creation time: {}\n", delta.toMilliSeconds()));
        }

        if (openedVideoDecoderTime != 0 && receiveTime != 0) {
            auto delta = openedVideoDecoderTime - receiveTime;
            debugInfo.append(std::format("Video decoder opening time: {}\n", delta.toMilliSeconds()));
        }

        if (pushVideoDecodeTime != 0 && receiveTime != 0) {
            auto delta = pushVideoDecodeTime - receiveTime;
            debugInfo.append(std::format("Video decode push time: {}\n", delta.toMilliSeconds()));
        }

        if (pushVideoRenderTime != 0 && receiveTime != 0) {
            auto delta = pushVideoRenderTime - receiveTime;
            debugInfo.append(std::format("Video render push time: {} \n", delta.toMilliSeconds()));
        }

        if (pushVideoRenderTime != 0 && receiveTime != 0) {
            auto delta = pushVideoRenderTime - receiveTime;
            debugInfo.append(std::format("Total time: {} ms\n", delta.toMilliSeconds()));
        }

        debugInfo.append("===============================\n");
        LogI("{}", debugInfo);
    }
};

class PlayerImplHelper {
public:
    bool createDataProvider(
        const std::string& path,
        ReaderDataCallBack callback
    ) noexcept;

    explicit PlayerImplHelper(std::weak_ptr<Player::Impl> player);

public:
    bool isRenderEnd() noexcept;

    bool seekToLastAvailableKeyframe(
        double targetTime
    ) noexcept;
public:
    PlayerDebugInfo debugInfo;
private:
    std::weak_ptr<Player::Impl> player_;
};

}
