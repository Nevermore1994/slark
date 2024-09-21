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

    std::tuple<bool, uint64_t> open(std::string_view probeData) noexcept override;

    void close() noexcept override;

    DemuxerResult parseData(std::unique_ptr<Data> data) override;
    
    [[nodiscard]] uint64_t getSeekToPos(CTime) override;

    inline static const DemuxerInfo& info() noexcept {
        static DemuxerInfo info = {
            DemuxerType::WAV,
            "WAVE",
            BaseClass<WAVDemuxer>::registerClass(GetClassName(WAVDemuxer))
        };
        return info;
    }

};

}





