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
            debugInfo.append(std::format("Player creation time: {:.3f} ms\n", delta.second() * 1000.0));
        }

        if (createdDemuxerTime != 0 && createdTime != 0) {
            auto delta = createdDemuxerTime - createdTime;
            debugInfo.append(std::format("Demuxer creation time: {:.3f} ms\n", delta.second() * 1000.0));
        }

        if (openedDemuxerTime != 0 && createdDemuxerTime != 0) {
            auto delta = openedDemuxerTime - createdDemuxerTime;
            debugInfo.append(std::format("Demuxer opening time: {:.3f} ms\n", delta.second() * 1000.0));
        }

        if (createAudioDecoderTime != 0 && openedDemuxerTime != 0) {
            auto delta = createAudioDecoderTime - openedDemuxerTime;
            debugInfo.append(std::format("Audio decoder creation time: {:.3f} ms\n", delta.second() * 1000.0));
        }

        if (openedAudioDecoderTime != 0 && createAudioDecoderTime != 0) {
            auto delta = openedAudioDecoderTime - createAudioDecoderTime;
            debugInfo.append(std::format("Audio decoder opening time: {:.3f} ms\n", delta.second() * 1000.0));
        }

        if (openedAudioDecoderTime != 0 && createAudioRenderTime != 0) {
            auto delta = createAudioRenderTime - openedAudioDecoderTime;
            debugInfo.append(std::format("Audio render creation time: {:.3f} ms\n", delta.second() * 1000.0));
        }

        if (createVideoDecoderTime != 0 && createAudioRenderTime != 0) {
            auto delta = createVideoDecoderTime - createAudioRenderTime;
            debugInfo.append(std::format("Video decoder creation time: {:.3f} ms\n", delta.second() * 1000.0));
        }

        if (openedVideoDecoderTime != 0 && createVideoDecoderTime != 0) {
            auto delta = openedVideoDecoderTime - createVideoDecoderTime;
            debugInfo.append(std::format("Video decoder opening time: {:.3f} ms\n", delta.second() * 1000.0));
        }

        if (pushVideoDecodeTime != 0 && openedVideoDecoderTime != 0) {
            auto delta = pushVideoDecodeTime - openedVideoDecoderTime;
            debugInfo.append(std::format("Video decode push time: {:.3f} ms\n", delta.second() * 1000.0));
        }

        if (pushVideoRenderTime != 0 && pushVideoDecodeTime != 0) {
            auto delta = pushVideoRenderTime - pushVideoDecodeTime;
            debugInfo.append(std::format("Video render push time: {:.3f} ms\n", delta.second() * 1000.0));
        }

        if (pushVideoRenderTime != 0 && receiveTime != 0) {
            auto delta = pushVideoRenderTime - receiveTime;
            debugInfo.append(std::format("Total time: {:.3f} ms\n", delta.second() * 1000.0));
        }

        debugInfo.append("===============================\n");
        LogI("{}", debugInfo);
    }
};

class PlayerImplHelper {
public:
    static bool createDataProvider(
        const std::string& path,
        void *impl,
        ReaderDataCallBack callback
    ) noexcept;

    explicit PlayerImplHelper(std::weak_ptr<Player::Impl> player);

public:
    bool openDemuxer() noexcept;

    bool appendProbeData(
        uint64_t offset,
        DataPtr ptr
    ) noexcept;

    void resetProbeData() noexcept;

    [[nodiscard]] DataView probeView() const noexcept;

    std::unique_ptr<Buffer>& probeBuffer() noexcept {
        return probeBuffer_;
    }

    bool isRenderEnd() noexcept;

    bool seekToLastAvailableKeyframe(
        double targetTime
    ) noexcept;
public:
    PlayerDebugInfo debugInfo;
private:
    void handleOpenMp4DemuxerResult(bool isSuccess) noexcept;

private:
    std::unique_ptr<Buffer> probeBuffer_;
    std::weak_ptr<Player::Impl> player_;
};

}
