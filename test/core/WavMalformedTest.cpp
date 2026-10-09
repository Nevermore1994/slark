//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>

#include "Mp4TestBytes.h"
#include "demuxer/WavDemuxer.h"

using namespace slark;
using namespace std::chrono_literals;
using slark::test::Bytes;
using slark::test::concat;
using slark::test::fourcc;
using slark::test::makeBuffer;
using slark::test::runDetached;

namespace {

void le16(Bytes& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void le32(Bytes& out, uint32_t v) {
    le16(out, static_cast<uint16_t>(v));
    le16(out, static_cast<uint16_t>(v >> 16));
}

Bytes wavFile(uint16_t format, uint16_t channels, uint32_t sampleRate, uint16_t bits, size_t dataBytes) {
    Bytes fmt;
    le16(fmt, format);
    le16(fmt, channels);
    le32(fmt, sampleRate);
    le32(fmt, sampleRate * channels * (bits / 8));
    le16(fmt, static_cast<uint16_t>(channels * (bits / 8)));
    le16(fmt, bits);

    Bytes file;
    fourcc(file, "RIFF");
    le32(file, static_cast<uint32_t>(4 + 8 + fmt.size() + 8 + dataBytes));
    fourcc(file, "WAVE");
    fourcc(file, "fmt ");
    le32(file, static_cast<uint32_t>(fmt.size()));
    file.insert(file.end(), fmt.begin(), fmt.end());
    fourcc(file, "data");
    le32(file, static_cast<uint32_t>(dataBytes));
    file.resize(file.size() + dataBytes, 0x11);
    return file;
}

std::optional<bool> openWithTimeout(Bytes file) {
    auto result = runDetached<bool>([file = std::move(file)]() {
        WAVDemuxer demuxer;
        auto buffer = makeBuffer(file);
        return demuxer.open(buffer);
    });
    if (result.wait_for(5s) != std::future_status::ready) {
        return std::nullopt;
    }
    return result.get();
}

}  // namespace

TEST(WavMalformedTest, MsGsmIsRejectedInsteadOfLoopingOnZeroFrameLength) {
    constexpr uint16_t kMsGsm = 0x0031;
    auto result = openWithTimeout(wavFile(kMsGsm, 1, 8000, 0, 650));
    ASSERT_TRUE(result.has_value()) << "open() did not finish";
    EXPECT_FALSE(*result);
}

TEST(WavMalformedTest, PcmStillOpens) {
    constexpr uint16_t kPcm = 0x0001;
    auto result = openWithTimeout(wavFile(kPcm, 2, 44100, 16, 4096));
    ASSERT_TRUE(result.has_value()) << "open() did not finish";
    EXPECT_TRUE(*result);
}

TEST(WavMalformedTest, PcmParseDataProducesFrames) {
    constexpr uint16_t kPcm = 0x0001;
    auto result = runDetached<size_t>([] {
        WAVDemuxer demuxer;
        auto buffer = makeBuffer(wavFile(kPcm, 2, 44100, 16, 16384));
        if (!demuxer.open(buffer)) {
            return size_t{0};
        }
        DataPacket packet;
        packet.offset = 0;
        packet.data = slark::test::makeData(Bytes(16384, 0x22));
        return demuxer.parseData(packet).audioFrames.size();
    });
    ASSERT_EQ(result.wait_for(5s), std::future_status::ready) << "parseData did not finish";
    EXPECT_GE(result.get(), 4u);
}
