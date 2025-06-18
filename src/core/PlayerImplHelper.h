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

    bool isLongStanceSeek(
        double playedTime,
        double demuxedTime,
        double targetTime
    ) noexcept;

    bool seekToLastAvailableKeyframe(
        double targetTime
    ) noexcept;

private:
    void handleOpenMp4DemuxerResult(bool isSuccess) noexcept;

private:
    std::unique_ptr<Buffer> probeBuffer_;
    std::weak_ptr<Player::Impl> player_;
};

}
