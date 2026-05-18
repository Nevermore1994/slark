//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AudioInfo.h"
#include "DataView.hpp"
#include "Range.h"
#include "Util.h"

#include <string>
#include <tuple>

namespace slark {

struct VideoInfo;

bool isNetworkLink(const std::string& path) noexcept;

bool isHlsLink(const std::string& url) noexcept;

bool findNaluUnit(DataView dataView, Range& range) noexcept;

// first first_mb_in_slice, second slice type
std::tuple<uint32_t, uint32_t> parseAvcSliceType(DataView naluView) noexcept;

std::tuple<uint32_t, uint32_t> parseHevcSliceType(DataView naluView, uint8_t naluType) noexcept;

void parseH264Sps(DataView bitstream, const std::shared_ptr<VideoInfo>& videoInfo) noexcept;

void parseH265Sps(DataView bitstream, const std::shared_ptr<VideoInfo>& videoInfo) noexcept;

/// only 1, 2, 3, 4, 5, 29
AudioProfile getAudioProfile(uint8_t profile) noexcept;

/// default 44100
int32_t getAACSamplingRate(int32_t index) noexcept;

} // namespace slark
