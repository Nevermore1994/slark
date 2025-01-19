//
// Created by Nevermore on 2022/5/27.
// slark MediaBase
// Copyright (c) 2022 Nevermore All rights reserved.
//

#pragma once

#include <string>
#include <tuple>
#include "Base.h"
#include "Util.hpp"
#include "DataView.h"

namespace slark {

struct VideoInfo;

bool isNetworkLink(const std::string& path) noexcept;

bool isHlsLink(const std::string& url) noexcept;

bool findNaluUnit(DataView dataView, Range& range) noexcept;

//first first_mb_in_slice, second slice type
std::tuple<uint32_t, uint32_t> parseSliceType(DataView naluView) noexcept;

void parseH264Sps(DataView bitstream, std::shared_ptr<VideoInfo> videoInfo) noexcept;

void parseH265Sps(DataView bitstream, std::shared_ptr<VideoInfo> videoInfo) noexcept;
}
