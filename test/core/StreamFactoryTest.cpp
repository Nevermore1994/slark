//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include "StreamFactory.hpp"

using namespace slark;

TEST(StreamFactoryTest, RecognizesPlaylistSuffixesCaseInsensitively) {
    EXPECT_TRUE(StreamFactory::isHLS("https://example.com/live/index.m3u8"));
    EXPECT_TRUE(StreamFactory::isHLS("https://example.com/live/index.M3U8"));
    EXPECT_TRUE(StreamFactory::isHLS("https://example.com/live/index.m3u"));
}

TEST(StreamFactoryTest, IgnoresQueryAndFragmentWhenRecognizingPlaylist) {
    EXPECT_TRUE(StreamFactory::isHLS("https://example.com/live/index.m3u8?token=abc"));
    EXPECT_TRUE(StreamFactory::isHLS("https://example.com/live/index.m3u8#variant"));
    EXPECT_TRUE(StreamFactory::isHLS("https://example.com/live/index.m3u8?token=abc#variant"));
    EXPECT_EQ(StreamFactory::detectStreamType("https://example.com/live/index.m3u8?token=abc"),
              StreamType::HLS);
}

TEST(StreamFactoryTest, RejectsNonPlaylistSuffixes) {
    EXPECT_FALSE(StreamFactory::isHLS("https://example.com/live/index.ts"));
    EXPECT_FALSE(StreamFactory::isHLS("https://example.com/live/index.m3u8.bak"));
    EXPECT_FALSE(StreamFactory::isHLS("https://example.com/live/index.m3u8.bak?token=abc"));
    EXPECT_FALSE(StreamFactory::isHLS(""));
}

// isHLS() deliberately ignores the scheme, so a local playlist is classified as HLS and
// gets an HLSStream. Pinned here because MediaUtil used to carry a second, stricter
// isHlsLink() that required an http(s) prefix and disagreed on this case.
TEST(StreamFactoryTest, RecognizesPlaylistBySuffixRegardlessOfScheme) {
    EXPECT_TRUE(StreamFactory::isHLS("/local/path/stream.m3u8"));
    EXPECT_TRUE(StreamFactory::isHLS("file:///tmp/stream.m3u8"));
    EXPECT_EQ(StreamFactory::detectStreamType("/local/path/stream.m3u8"), StreamType::HLS);
}

TEST(StreamFactoryTest, DetectsHttpAndLocalStreamsWithoutPlaylistSuffix) {
    EXPECT_EQ(StreamFactory::detectStreamType("https://example.com/video.mp4"), StreamType::Http);
    EXPECT_EQ(StreamFactory::detectStreamType("http://example.com/video.ts"), StreamType::Http);
    EXPECT_EQ(StreamFactory::detectStreamType("../../test_sample/sample-3s.wav"),
              StreamType::File);
    EXPECT_EQ(StreamFactory::detectStreamType("file:///tmp/video.mp4"), StreamType::File);
}
