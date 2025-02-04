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

namespace slark {

class PlayerImplHelper {
public:
    static bool createDataProvider(const std::string& path, void* impl, ReaderDataCallBack callback) noexcept;
    
    PlayerImplHelper(std::weak_ptr<Player::Impl> player);
    
public:
    bool openDemuxer() noexcept;

    bool appendProbeData(uint64_t offset, DataPtr ptr) noexcept;
    
    void resetProbeData() noexcept;
    
    DataView probeView() const noexcept;
    
    std::unique_ptr<Buffer>& probeBuffer() noexcept {
        return probeBuffer_;
    }
private:
    void handleOpenMp4DemuxerResult(bool isSuccess) noexcept;
private:
    std::unique_ptr<Buffer> probeBuffer_;
    std::weak_ptr<Player::Impl> player_;
};

}
