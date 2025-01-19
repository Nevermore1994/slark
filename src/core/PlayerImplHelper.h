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

namespace slark {

class PlayerImplHelper {
public:
    static bool createDataProvider(const std::string& path, void* impl, ReaderDataCallBack callback) noexcept;
    
    void handleOpenDemuxerResult(bool isSuccess, void* impl) noexcept;
public:
    bool appendProbeData(uint64_t offset, DataPtr ptr) noexcept;
    
    void resetProbeData() noexcept;
    
    DataView probeView() const noexcept;
    
    std::unique_ptr<Buffer>& probeBuffer() noexcept {
        return probeBuffer_;
    }
private:
    void handleOpenMp4DemuxerResult(bool isSuccess, void* impl) noexcept;
private:
    std::unique_ptr<Buffer> probeBuffer_;
};

}
