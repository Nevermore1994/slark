//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <string>
#include <tuple>

#include "AudioInfo.h"
#include "DataView.hpp"
#include "Range.h"
#include "Util.h"

namespace slark {

struct VideoInfo;

bool isNetworkLink(const std::string& path) noexcept;

bool isHlsLink(const std::string& url) noexcept;

bool findNaluUnit(DataView dataView, Range& range) noexcept;

std::tuple<uint32_t, uint32_t> parseAvcSliceType(DataView naluView) noexcept;

std::tuple<uint32_t, uint32_t> parseHevcSliceType(DataView naluView, uint8_t naluType) noexcept;

void parseH264Sps(DataView bitstream, const std::shared_ptr<VideoInfo>& videoInfo) noexcept;

void parseH265Sps(DataView bitstream, const std::shared_ptr<VideoInfo>& videoInfo) noexcept;

AudioProfile getAudioProfile(uint8_t profile) noexcept;

int32_t getAACSamplingRate(int32_t index) noexcept;

}  // namespace slark
