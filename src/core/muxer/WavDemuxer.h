//
// Created by Nevermore on 2022/5/23.
// slark WAVDemuxer
// Copyright (c) 2022 Nevermore All rights reserved.
//
#pragma once

#include "IDemuxer.h"
#include <string_view>

namespace slark {

class WAVDemuxer: public IDemuxer {
public:
    WAVDemuxer();

    ~WAVDemuxer() override;

    bool open(std::unique_ptr<Buffer>& ) noexcept override;

    void close() noexcept override;

    DemuxerResult parseData(std::unique_ptr<Data> data, int64_t offset) noexcept override;
    
    [[nodiscard]] uint64_t getSeekToPos(Time::TimePoint) noexcept override;
    
    void reset() noexcept override;

    inline static const DemuxerInfo& info() noexcept {
        static DemuxerInfo info = {
            DemuxerType::WAV,
            "WAVE",
            BaseClass<WAVDemuxer>::registerClass(GetClassName(WAVDemuxer))
        };
        return info;
    }
private:
    uint32_t parsedFrameCount_ = 0;
};

}





