//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <string_view>

#include "IDemuxer.h"

namespace slark {

enum class WaveFormat : uint16_t {
    PCM = 0x0001,
    ALAW = 0x0006,
    MULAW = 0x0007,
    MSGSM = 0x0031,
    EXTENSIBLE = 0xFFFE,
};

class WAVDemuxer : public IDemuxer {
 public:
    WAVDemuxer();

    ~WAVDemuxer() override;

    bool open(std::unique_ptr<Buffer>&) noexcept override;

    void close() noexcept override;

    DemuxerResult parseData(DataPacket& packet) noexcept override;

    [[nodiscard]] uint64_t getSeekToPos(double time) noexcept override;

    void reset() noexcept override;

    inline static const DemuxerInfo& info() noexcept {
        static DemuxerInfo info = {DemuxerType::WAV, "WAVE",
                                   Reflection::enroll<WAVDemuxer>("WAVDemuxer"), 8};
        return info;
    }

 private:
    struct FmtChunk {
        WaveFormat format = WaveFormat::PCM;
        uint16_t channels = 0;
        uint64_t sampleRate = 0;
        uint16_t bitsPerSample = 0;
        uint32_t channelMask = 0;
    };

    bool parseFmtChunk(DataView data, uint64_t offset, uint32_t chunkSize, FmtChunk& out) noexcept;

    bool parseExtensibleFormat(DataView formatSpec, FmtChunk& out) noexcept;

    static bool readFmtCommonFields(DataView formatSpec, FmtChunk& out) noexcept;

    static bool validateBitsPerSample(WaveFormat format, uint16_t bitsPerSample) noexcept;

    AVFramePtr makeAudioFrame(uint64_t dataLength,
                              double scale,
                              const std::shared_ptr<AudioFrameInfo>& frameInfo) noexcept;

    uint32_t parsedFrameCount_ = 0;
};

}  // namespace slark
