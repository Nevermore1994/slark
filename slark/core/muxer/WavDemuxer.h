//
// Created by Nevermore on 2022/5/23.
// slark WAVDemuxer
// Copyright (c) 2022 Nevermore All rights reserved.
//
#pragma once

#include "IDemuxer.h"
#include "BaseClass.hpp"
#include <string_view>

namespace slark {

class WAVDemuxer: public IDemuxer {
public:
    WAVDemuxer();

    ~WAVDemuxer() override;

    std::tuple<bool, uint64_t> open(std::string_view probeData) noexcept override;

    void close() noexcept override;

    void reset() noexcept override;

    std::tuple<DemuxerState, AVFrameArray> parseData(std::unique_ptr<Data> data) override;

    inline static DemuxerInfo& info()  noexcept {
        static DemuxerInfo info = {
            DemuxerType::WAV,
            "WAVE",
            BaseClass<WAVDemuxer>::registerClass()
        };
        return info;
    }

};

}





