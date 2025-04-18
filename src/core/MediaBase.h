//
// Created by Nevermore on 2024/5/27.
// slark MediaBase
// Copyright (c) 2024 Nevermore All rights reserved.
//

#pragma once

#include <string>
#include <tuple>
#include "Base.h"
#include "Util.hpp"
#include "DataView.h"
#include "AudioDefine.h"

namespace slark {

struct VideoInfo;

bool isNetworkLink(const std::string& path) noexcept;

bool isHlsLink(const std::string& url) noexcept;

bool findNaluUnit(DataView dataView, Range& range) noexcept;

//first first_mb_in_slice, second slice type
std::tuple<uint32_t, uint32_t> parseSliceType(DataView naluView) noexcept;

void parseH264Sps(DataView bitstream, const std::shared_ptr<VideoInfo>& videoInfo) noexcept;

void parseH265Sps(DataView bitstream, const std::shared_ptr<VideoInfo>& videoInfo) noexcept;

AudioProfile getAudioProfile(uint8_t profile);

int32_t getAACSamplingRate(int32_t index);
}
