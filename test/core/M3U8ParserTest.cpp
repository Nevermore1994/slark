//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "M3U8Parser.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "MediaPlaylist.h"

using namespace slark;

TEST(M3U8ParserTest, ParseMediaPlaylist) {
    constexpr std::string_view kM3U8 = "#EXTM3U\n"
                                       "#EXTINF:4.0,\n"
                                       "segment0.ts\n"
                                       "#EXTINF:4.0,\n"
                                       "segment1.ts\n"
                                       "#EXT-X-ENDLIST\n";

    M3U8Parser parser("https://example.com/live");
    ASSERT_TRUE(parser.parse(std::string(kM3U8)));
    ASSERT_FALSE(parser.isMasterPlaylist());

    auto playlist = parser.playlist();
    ASSERT_NE(playlist, nullptr);
    EXPECT_FALSE(playlist->isLive());
    EXPECT_DOUBLE_EQ(playlist->totalDuration(), 8.0);
    EXPECT_EQ(playlist->segments().size(), 2u);
}

TEST(M3U8ParserTest, ParseMasterPlaylist) {
    constexpr std::string_view kM3U8 = "#EXTM3U\n"
                                       "#EXT-X-STREAM-INF:BANDWIDTH=1280000,RESOLUTION=854x480\n"
                                       "mid_480p.m3u8\n"
                                       "#EXT-X-STREAM-INF:BANDWIDTH=2560000,RESOLUTION=1920x1080\n"
                                       "mid_1080p.m3u8\n"
                                       "#EXT-X-ENDLIST\n";

    M3U8Parser parser("https://example.com/master");
    ASSERT_TRUE(parser.parse(std::string(kM3U8)));
    ASSERT_TRUE(parser.isMasterPlaylist());

    auto playlist = parser.playlist();
    ASSERT_NE(playlist, nullptr);
    EXPECT_TRUE(playlist->hasVariants());
    EXPECT_EQ(playlist->variants().size(), 2u);
}

TEST(M3U8ParserTest, ByteRangeSupport) {
    constexpr std::string_view kM3U8 = "#EXTM3U\n"
                                       "#EXTINF:4.0,\n"
                                       "segment0.ts\n"
                                       "#EXT-X-BYTERANGE:1000@200\n"
                                       "#EXTINF:4.0,\n"
                                       "segment1.ts\n"
                                       "#EXT-X-BYTERANGE:500\n"
                                       "#EXTINF:4.0,\n"
                                       "segment2.ts\n"
                                       "#EXT-X-ENDLIST\n";

    M3U8Parser parser("https://example.com/live");
    ASSERT_TRUE(parser.parse(std::string(kM3U8)));

    const auto& segments = parser.playlist()->segments();
    ASSERT_EQ(segments.size(), 3u);

    // #EXT-X-BYTERANGE applies to the following URI (segment)
    // segment0 has no preceding BYTERANGE
    EXPECT_FALSE(segments[0].byteRange.has_value());
    // segment1 has #EXT-X-BYTERANGE:1000@200
    EXPECT_TRUE(segments[1].byteRange.has_value());
    EXPECT_EQ(segments[1].byteRange->pos, 200u);
    EXPECT_EQ(segments[1].byteRange->size, 1000u);
    // segment2 has #EXT-X-BYTERANGE:500 (offset = 200 + 1000 = 1200)
    EXPECT_TRUE(segments[2].byteRange.has_value());
    EXPECT_EQ(segments[2].byteRange->pos, 1200u);
    EXPECT_EQ(segments[2].byteRange->size, 500u);
}

TEST(M3U8ParserTest, StreamInfParsesBandwidth) {
    constexpr std::string_view kM3U8 =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:CODECS=\"avc1\",BANDWIDTH=2560000,RESOLUTION=1920x1080\n"
        "main_1080p.m3u8\n"
        "#EXT-X-ENDLIST\n";

    M3U8Parser parser("https://example.com/master");
    ASSERT_TRUE(parser.parse(std::string(kM3U8)));

    const auto& variants = parser.playlist()->variants();
    ASSERT_EQ(variants.size(), 1u);
    EXPECT_EQ(variants[0].bandwidth, 2560000u);
    EXPECT_TRUE(variants[0].width.has_value());
    EXPECT_EQ(variants[0].width.value(), 1920u);
}
