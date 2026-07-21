#include "IStream.h"
#include "AudioInfo.h"
#include "VideoInfo.h"
#include "Data.h"
#include "Range.h"

#include <gtest/gtest.h>

using namespace slark;

TEST(InterfaceContractTest, AudioInfoDefaults) {
    AudioInfo info;
    EXPECT_EQ(info.sampleRate, 0u);
    EXPECT_EQ(info.channels, 0u);
}

TEST(InterfaceContractTest, VideoInfoDefaults) {
    VideoInfo info;
    EXPECT_EQ(info.width, 0u);
    EXPECT_EQ(info.height, 0u);
}

TEST(InterfaceContractTest, RangeDefaults) {
    Range range;
    EXPECT_FALSE(range.pos.has_value());
    EXPECT_FALSE(range.size.has_value());
}

TEST(InterfaceContractTest, DataCreate) {
    auto data = Data::create(100);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->size(), 100u);
}

TEST(InterfaceContractTest, StreamResultDefaults) {
    StreamResult<void> result;
    EXPECT_TRUE(result.has_value());
}
