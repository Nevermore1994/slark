//
// Created by Nevermore on 2022/5/27.
// slark MediaBase
// Copyright (c) 2022 Nevermore All rights reserved.
//

#include <regex>
#include "MediaUtil.h"
#include "Log.hpp"
#include "VideoInfo.h"

#define ReadUINT8(view, offset, count)  static_cast<uint8_t>(Golomb::readBits(view, offset, count))

namespace slark {

constexpr uint32_t kInvalidPos = static_cast<uint32_t>(std::string_view::npos);

bool isNetworkLink(
    const std::string& path
) noexcept {
    constexpr std::string_view kHttpRegex = "^(http|https):\\/\\/.*";
    std::regex regex(kHttpRegex.data());
    return std::regex_match(path, regex);
}

bool isHlsLink(
    const std::string& url
) noexcept {
    const std::regex hlsRegex(R"(^(http|https)://.*\.m3u8(\?.*)?$)", std::regex::icase);
    return std::regex_match(url, hlsRegex);
}

AudioProfile getAudioProfile(
    uint8_t profile
) noexcept {
    if ((profile < 1 || profile >= 6) && profile != 29) {
        return AudioProfile::AAC_LC;
    }
    auto aacProfile = static_cast<AudioProfile>(profile);
    return aacProfile;
}

int32_t getAACSamplingRate(
    int32_t index
) noexcept {
    static const std::unordered_map<int32_t, int32_t> samplingRateTable = {
            {0, 96000}, {1, 88200}, {2, 64000}, {3, 48000},
            {4, 44100}, {5, 32000}, {6, 24000}, {7, 22050},
            {8, 16000}, {9, 12000}, {10, 11025}, {11, 8000},
            {12, 7350}
    };

    auto it = samplingRateTable.find(index);
    if (it != samplingRateTable.end()) {
        return it->second;
    } else {
        return 44100;
    }
}

uint32_t findNaluStartCode(
    DataView dataView
) noexcept {
    static const auto func = [](DataView view) {
        uint32_t p = 0;
        while(p + 2 < view.length()) {
            if (view[p + 0] == 0 &&
                view[p + 1] == 0 &&
                view[p + 2] == 1) {
                return p;
            }
            p++;
        }
        return kInvalidPos;
    };
    auto pos = func(dataView);
    if (0 < pos && pos < dataView.length() && dataView[pos - 1] == 0) {
        return pos - 1;
    }
    return pos;
}

bool findNaluUnit(
    DataView dataView,
    Range& range
) noexcept {
    auto start = findNaluStartCode(dataView);
    if (start == kInvalidPos) {
        return false;
    }
    while (dataView[start++] == 0);
    auto remainView = dataView.substr(start);
    auto size = findNaluStartCode(remainView);
    if (size == kInvalidPos) {
        size = static_cast<uint32_t>(remainView.size());
    }
    range.pos = start;
    range.size = size;
    return true;
}

std::tuple<uint32_t, uint32_t> parseAvcSliceType(
    DataView naluView
) noexcept {
    using namespace Util;
    int32_t offset = 0;
    auto sliceView = naluView.substr(1);//skip nalu header
    uint32_t firstMBInSlice = Golomb::readUe(sliceView, offset);
    uint32_t sliceType = Golomb::readUe(sliceView, offset);
    return {firstMBInSlice, sliceType};
}

std::tuple<uint32_t, uint32_t> parseHevcSliceType(
    DataView naluView,
    uint8_t naluType
) noexcept {
    using namespace Util;
    int32_t offset = 0;
    auto sliceView = naluView.substr(2); // skip NALU header (2 bytes)
    // first_slice_segment_in_pic_flag (1 bit)
    Golomb::readBits(sliceView, offset, 1);
    // no_output_of_prior_pics_flag (1 bit, only for IDR)
    if (naluType == 19 || naluType == 20) {
        Golomb::readBits(sliceView, offset, 1);
    }
    // slice_segment_address (UE)
    uint32_t sliceSegmentAddr = Golomb::readUe(sliceView, offset);
    // slice_type (UE)
    uint32_t sliceType = Golomb::readUe(sliceView, offset);
    return {sliceSegmentAddr, sliceType};
}

void parseHrd(
    DataView bitstream,
    int32_t& offset
) {
    using namespace Util;
    auto cpbCntMinus1 = Golomb::readUe(bitstream, offset);
    offset += 4; // bit_rate_scale
    offset += 4; // cpb_size_scale
    
    for (uint32_t i = 0; i < cpbCntMinus1 + 1; i++) {
        Golomb::readUe(bitstream, offset);
        Golomb::readUe(bitstream, offset);
        Golomb::readBits(bitstream, offset, 1);
    }
    offset += 5;
    offset += 5;
    offset += 5;
    offset += 5;
}

double parseH264Vui(
    DataView bitstream,
    int32_t& offset
) {
    using namespace Util;
    double fps = 0.0;
    do {
        auto vuiParametersPresentFlag = ReadUINT8(bitstream, offset, 1);
        if (!vuiParametersPresentFlag) {
            break;
        }
        
        auto aspectRatioInfoPresentFlag = ReadUINT8(bitstream, offset, 1);
        // Skip aspect ratio and overscan info for now (can be expanded)
        if (aspectRatioInfoPresentFlag) {
            //aspectRatioIdc
            auto aspectRatioIdc = Golomb::readBits(bitstream, offset, 8);
            if (aspectRatioIdc == 255) {
                offset += 16; //sar width
                offset += 16; //sar height
            }
        }
        
        auto overscanInfoPresentFlag = ReadUINT8(bitstream, offset, 1);
        if (overscanInfoPresentFlag) {
            offset += 1;
        }
        
        auto videoSignalTypePresentFlag = ReadUINT8(bitstream, offset, 1);
        if (videoSignalTypePresentFlag) {
            // Skip video_signal_type (can be expanded)
            offset += 3; //videoFormat
            offset += 1; //videoFullRangeFlag
            auto colorFlag = Golomb::readBits(bitstream, offset, 1);
            if (colorFlag) {
                offset += 8; //color primaries
                offset += 8; //transfer character
                offset += 8; //matrix corfficuents
            }
        }
        
        auto chromaInfoFlag = Golomb::readBits(bitstream, offset, 1);
        if (chromaInfoFlag) {
            Golomb::readUe(bitstream, offset); //chroma type top field
            Golomb::readUe(bitstream, offset); //chroma type bottom field
        }
        
        auto frameRateInfoPresentFlag = ReadUINT8(bitstream, offset, 1);
        if (frameRateInfoPresentFlag) {
            auto timeScale = Golomb::readBits(bitstream, offset, 32);
            uint32_t numUnitsInTick = Golomb::readBits(bitstream, offset, 32);
            auto fixFlag = Golomb::readBits(bitstream, offset, 1);
            if (fixFlag) {
                fps = static_cast<double>(timeScale) / static_cast<double>(numUnitsInTick);
            }
        }
        auto nalHrdParametersPresentFlag = Golomb::readBits(bitstream, offset, 1); //nal_hrd_parameters_present_flag
        if (nalHrdParametersPresentFlag) {
            parseHrd(bitstream, offset);
        }
        auto vclHrdParametersPresentFlag = Golomb::readBits(bitstream, offset, 1);
        if (vclHrdParametersPresentFlag) {
            parseHrd(bitstream, offset);
        }
        
        if (nalHrdParametersPresentFlag || vclHrdParametersPresentFlag) {
            Golomb::readBits(bitstream, offset, 1);
        }
        auto picStructPresentFlag = Golomb::readBits(bitstream, offset, 1);
        if (picStructPresentFlag) {
            fps /= 2;
        }
    } while (false);
    return fps;
}

// Modify parseSps to call parseVui
void parseH264Sps(
    DataView bitstream,
    const std::shared_ptr<VideoInfo>& videoInfo
) noexcept {
    using namespace Util;
    bitstream = bitstream.substr(1); //skip header
    int32_t offset = 0;
    auto profileIdc = ReadUINT8(bitstream, offset, 8);
    offset += 8; //skip flags + reserved
    videoInfo->profile = profileIdc;
    [[maybe_unused]] auto levelIdc = ReadUINT8(bitstream, offset, 8);
    videoInfo->level = levelIdc;
    [[maybe_unused]] uint32_t seqParameterSetId = Golomb::readUe(bitstream, offset);
    uint32_t chromaFormatIdc = 1; //default 420
    static const std::vector<uint8_t> kSpecialProfile = {100, 110, 122, 244, 44, 83, 86, 118, 128};
    if (std::find(kSpecialProfile.begin(), kSpecialProfile.end(), profileIdc) != kSpecialProfile.end()) {
        chromaFormatIdc = Golomb::readUe(bitstream, offset);
        if (chromaFormatIdc == 3) {
            offset += 1; //separate_colour_plane_flag
        }
        Golomb::readUe(bitstream, offset); // bit_depth_luma_minus8
        Golomb::readUe(bitstream, offset); // bit_depth_chroma_minus8
        offset += 1;// qpprime_y_zero_transform_bypass_flag
        auto seqScalingMatrixPresentFlag = Golomb::readBits(bitstream, offset, 1);// seq_scaling_matrix_present_flag
        if (seqScalingMatrixPresentFlag) {
            for (auto i = 0; i < (chromaFormatIdc != 3 ? 8 : 12); i++) {
                seqScalingMatrixPresentFlag = Golomb::readBits(bitstream, offset, 1);// seq_scaling_list_present_flag
                if (seqScalingMatrixPresentFlag) {
                    auto count = i < 6 ? 4 : 8;
                    offset += count * count;
                }
            }
        }
    }
    
    // Additional fields (Chroma Format, Bit Depth, etc.) would be here
    [[maybe_unused]] uint32_t log2MaxFrameNumMinus4 = Golomb::readUe(bitstream, offset);
    uint32_t picOrderCntType = Golomb::readUe(bitstream, offset);

    if (picOrderCntType == 0) {
        uint32_t log2MaxPicOrderCntLsbMinus4 = Golomb::readUe(bitstream, offset);
        LogI("log2MaxPicOrderCntLsbMinus4:{}", log2MaxPicOrderCntLsbMinus4);
    } else if (picOrderCntType == 1) {
        auto deltaPicOrderAlwaysZeroFlag = ReadUINT8(bitstream, offset, 1);
        auto offsetForNonRefPic = static_cast<int32_t>(Golomb::readUe(bitstream, offset));
        auto offsetForTopToBottomField = static_cast<int32_t>(Golomb::readUe(bitstream, offset));
        auto numRefFramesInPicOrderCntCycle = Golomb::readUe(bitstream, offset);

        LogI("deltaPicOrderAlwaysZeroFlag:{}, offsetForNonRefPic:{}, "
             "offsetForTopToBottomField:{}, numRefFramesInPicOrderCntCycle:{}",
             deltaPicOrderAlwaysZeroFlag, offsetForNonRefPic,
             offsetForTopToBottomField, numRefFramesInPicOrderCntCycle);
    }

    uint32_t numRefFrames = Golomb::readUe(bitstream, offset);
    [[maybe_unused]] auto gapsInFrameNumValueAllowedFlag = ReadUINT8(bitstream, offset, 1);
    uint32_t picWidthInMbsMinus1 = Golomb::readUe(bitstream, offset);
    uint32_t picHeightInMapUnitsMinus1 = Golomb::readUe(bitstream, offset);

    LogI("numRefFrames:{}, picWidthInMbsMinus1:{}, picHeightInMapUnitsMinus1:{}", numRefFrames, picWidthInMbsMinus1, picHeightInMapUnitsMinus1);

    uint32_t width = (picWidthInMbsMinus1 + 1) * 16;
    uint32_t height = (picHeightInMapUnitsMinus1 + 1) * 16;

    videoInfo->width = width;
    videoInfo->height = height;

    auto frameMbsOnlyFlag = Golomb::readBits(bitstream, offset, 1); //frame_mbs_only_flag
    if (frameMbsOnlyFlag == 0) {
        offset += 1;
    }
    offset += 1; //skip direct_8x8_inference_flag
    auto cropFlag = Golomb::readBits(bitstream, offset, 1);
    if (cropFlag) {
        auto leftOffset = Golomb::readUe(bitstream, offset);
        auto rightOffset = Golomb::readUe(bitstream, offset);
        auto topOffset = Golomb::readUe(bitstream, offset);
        auto bottomOffset = Golomb::readUe(bitstream, offset);
        
        unsigned int subWidth = 2;
        unsigned int subHeight = 2;
        //default 420
        if (0 == chromaFormatIdc) // monochrome
        {
            subWidth = 1;
            subHeight = 2 - frameMbsOnlyFlag;
        } else if (2 == chromaFormatIdc) {
            // 4:2:2
            subWidth = 2;
            subHeight = 2 - frameMbsOnlyFlag;
        }
        else if (3 == chromaFormatIdc) {
            // 4:4:4
            subWidth = 1;
            subHeight = 2 - frameMbsOnlyFlag;
        }
            
        videoInfo->width  -= subWidth * (leftOffset + rightOffset);
        videoInfo->height = (2 - frameMbsOnlyFlag) * videoInfo->height - subHeight * (topOffset + bottomOffset);
    }
    // Parse VUI for FPS
    LogI("width:{}, height:{}", width, height);
    videoInfo->fps = static_cast<uint16_t>(parseH264Vui(bitstream, offset));
}

double parseH265Vui(DataView bitstream, int32_t& offset) noexcept {
    using namespace Util;
    do {
        auto vuiParametersPresentFlag = ReadUINT8(bitstream, offset, 1);
        if (!vuiParametersPresentFlag) {
            break;
        }
        auto frameRateInfoPresentFlag = ReadUINT8(bitstream, offset, 1);
        if (!frameRateInfoPresentFlag) {
            break;
        }
        uint32_t timeScale = Golomb::readBits(bitstream, offset, 32);
        uint32_t numUnitsInTick = Golomb::readBits(bitstream, offset, 32);

        auto fps = static_cast<double>(timeScale) / numUnitsInTick;
        LogI("Frame rate (fps):{} ", fps);
        return fps;
    } while (false);
    return 0;
}

void profileTierLevel(
    DataView bitstream,
    int32_t& offset,
    uint8_t profilePresentFlag,
    uint32_t maxNumSubLayersMinus
) {
    using namespace Util;
    if (profilePresentFlag) {
        offset += 2; // general_profile_space
        offset += 1; // general_tier_flag
        offset += 5; //generalProfileIdc
        offset += 5; // general_profile_idc
        offset += 32;// general_profile_compatibility_flags
        
        offset += 1; // general_progressive_source_flag
        offset += 1; // general_interlaced_source_flag
        offset += 1; // general_non_packed_constraint_flag
        offset += 1; // general_frame_only_constraint_flag

        offset += 43; // general_reserved_zero_43bits
        
        offset += 1; // general_reserved_zero_bit
    }

    offset += 8; // general_level_idc

    std::vector<uint32_t> subLayerProfilePresentFlags;
    for (uint32_t i = 0; i < maxNumSubLayersMinus; i++) {
        auto subLayerProfilePresentFlag = Util::Golomb::readBits(bitstream, offset, 1);
        offset += 1; // sub_layer_level_present_flag
        subLayerProfilePresentFlags.push_back(subLayerProfilePresentFlag);
    }

    if (maxNumSubLayersMinus > 0) {
        for (auto i = maxNumSubLayersMinus; i < 8; i++) {
            offset += 2; // reserved_zero_2bits
        }
    }

    for (uint32_t i = 0; i < maxNumSubLayersMinus; i++) {
        if (subLayerProfilePresentFlags[i]) {
            offset += 2; // sub_layer_profile_space
            offset += 1; // sub_layer_tier_flag
            offset += 5; // sub_layer_profile_idc
            offset += 32; // sub_layer_profile_compatibility_flag
            offset += 1; // sub_layer_progressive_source_flag
            offset += 1; // sub_layer_interlaced_source_flag
            offset += 1; // sub_layer_non_packed_constraint_flag
            offset += 1; // sub_layer_frame_only_constraint_flag

            offset += 43; // sub_layer_reserved_zero_43bits

            offset += 1; // sub_layer_reserved_zero_bit
            offset += 8; // sub_layer_level_idc
        }
    }
}

void parseH265Sps(
    DataView bitstream,
    const std::shared_ptr<VideoInfo>& videoInfo
) noexcept {
    using namespace Util;
    int32_t offset = 0;
    offset += 8;//skip header
    offset += 4;//sps_video_parameter_set_id
    auto maxSubLayers = Golomb::readBits(bitstream, offset, 3);//sps_max_sub_layers_minus1
    offset += 1;//sps_temporal_id_nesting_flag
    profileTierLevel(bitstream, offset, 1, maxSubLayers);
    
    Golomb::readUe(bitstream, offset);//sps_seq_parameter_set_id
    auto chromaFormatIdc = Golomb::readUe(bitstream, offset); // chroma_format_idc
    uint32_t separateColourPlaneFlag = 0;
    if (chromaFormatIdc) {
        separateColourPlaneFlag = Golomb::readBits(bitstream, offset, 1); // separate_colour_plane_flag
    }
    uint32_t picWidthInMbsMinus = Golomb::readUe(bitstream, offset);
    uint32_t picHeightInMapUnitsMinus = Golomb::readUe(bitstream, offset);
    auto conformanceWindowFlag = Golomb::readBits(bitstream, offset, 1); //conformance_window_flag
    if (conformanceWindowFlag) {
        auto leftOffset = Golomb::readUe(bitstream, offset);
        auto rightOffset = Golomb::readUe(bitstream, offset);
        auto topOffset = Golomb::readUe(bitstream, offset);
        auto bottomOffset = Golomb::readUe(bitstream, offset);
        auto subWidth = ( (1 == chromaFormatIdc) ||(2 == chromaFormatIdc)) && (0 == separateColourPlaneFlag) ? 2 : 1;
        auto subHeight = (1 == chromaFormatIdc) && (0 == separateColourPlaneFlag) ? 2 : 1;
        picWidthInMbsMinus  -= (static_cast<uint32_t>(subWidth) * leftOffset + static_cast<uint32_t>(subWidth) * rightOffset);
        picHeightInMapUnitsMinus -= (static_cast<uint32_t>(subHeight) * topOffset + static_cast<uint32_t>(subHeight) * bottomOffset);
    }
    videoInfo->width = picWidthInMbsMinus;
    videoInfo->height = picHeightInMapUnitsMinus;
    // Parse VUI for FPS
    videoInfo->fps = static_cast<uint16_t>(parseH265Vui(bitstream, offset));
}

}//end namespace slark
