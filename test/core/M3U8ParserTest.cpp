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
    EXPECT_EQ(playlist->mediaSequence(), 0u);
    EXPECT_DOUBLE_EQ(playlist->targetDuration(), 0.0);
    EXPECT_EQ(playlist->segments().size(), 2u);
    EXPECT_EQ(playlist->segments()[0].sequence, 0u);
    EXPECT_EQ(playlist->segments()[1].sequence, 1u);
}

TEST(M3U8ParserTest, ParsesLiveWindowMetadataAndStableSequence) {
    constexpr std::string_view firstPlaylist = "#EXTM3U\n"
                                                "#EXT-X-TARGETDURATION:6\n"
                                                "#EXT-X-MEDIA-SEQUENCE:100\n"
                                                "#EXTINF:6.0,\n"
                                                "segment100.ts\n"
                                                "#EXTINF:6.0,\n"
                                                "segment101.ts\n";
    constexpr std::string_view secondPlaylist = "#EXTM3U\n"
                                                 "#EXT-X-TARGETDURATION:4\n"
                                                 "#EXT-X-MEDIA-SEQUENCE:101\n"
                                                 "#EXTINF:4.0,\n"
                                                 "segment101.ts\n"
                                                 "#EXTINF:4.0,\n"
                                                 "segment102.ts\n";

    M3U8Parser parser("https://example.com/live/");
    ASSERT_TRUE(parser.parse(std::string(firstPlaylist)));
    auto first = parser.playlist();
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE(first->isLive());
    EXPECT_EQ(first->mediaSequence(), 100u);
    EXPECT_DOUBLE_EQ(first->targetDuration(), 6.0);
    ASSERT_EQ(first->segments().size(), 2u);
    EXPECT_EQ(first->segments()[0].sequence, 100u);
    EXPECT_EQ(first->segments()[1].sequence, 101u);

    ASSERT_TRUE(parser.parse(std::string(secondPlaylist)));
    auto second = parser.playlist();
    ASSERT_NE(second, nullptr);
    EXPECT_TRUE(second->isLive());
    EXPECT_EQ(second->mediaSequence(), 101u);
    EXPECT_DOUBLE_EQ(second->targetDuration(), 4.0);
    ASSERT_EQ(second->segments().size(), 2u);
    EXPECT_EQ(second->segments()[0].sequence, 101u);
    EXPECT_EQ(second->segments()[1].sequence, 102u);
    EXPECT_EQ(second->findSegmentBySequence(101), 0u);
    EXPECT_EQ(second->findSegmentBySequence(100), std::nullopt);
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

    EXPECT_FALSE(segments[0].byteRange.has_value());
    EXPECT_TRUE(segments[1].byteRange.has_value());
    EXPECT_EQ(segments[1].byteRange->pos, 200u);
    EXPECT_EQ(segments[1].byteRange->size, 1000u);
    EXPECT_TRUE(segments[2].byteRange.has_value());
    EXPECT_EQ(segments[2].byteRange->pos, 1200u);
    EXPECT_EQ(segments[2].byteRange->size, 500u);
}

TEST(M3U8ParserTest, SupportsStandardByteRangeTagOrdering) {
    constexpr std::string_view kM3U8 = "#EXTM3U\n"
                                       "#EXT-X-TARGETDURATION:4\n"
                                       "#EXTINF:4.0,\n"
                                       "#EXT-X-BYTERANGE:1000@200\n"
                                       "asset.ts\n"
                                       "#EXTINF:4.0,\n"
                                       "#EXT-X-BYTERANGE:500\n"
                                       "asset.ts\n"
                                       "#EXT-X-ENDLIST\n";

    M3U8Parser parser("https://example.com/media/index.m3u8");
    ASSERT_TRUE(parser.parse(std::string(kM3U8)));
    const auto& segments = parser.playlist()->segments();
    ASSERT_EQ(segments.size(), 2u);
    ASSERT_TRUE(segments[0].byteRange.has_value());
    EXPECT_EQ(segments[0].byteRange->start(), 200u);
    EXPECT_EQ(segments[0].byteRange->size, 1000);
    EXPECT_EQ(segments[0].uri, "https://example.com/media/asset.ts");
    ASSERT_TRUE(segments[1].byteRange.has_value());
    EXPECT_EQ(segments[1].byteRange->start(), 1200u);
    EXPECT_EQ(segments[1].byteRange->size, 500);
    EXPECT_EQ(segments[1].uri, "https://example.com/media/asset.ts");
}

TEST(M3U8ParserTest, ResolvesPlaylistReferencesAccordingToUriPathRules) {
    constexpr std::string_view base =
        "https://cdn.example.com/a/b/master.m3u8?token=old";

    EXPECT_EQ(M3U8Parser::resolveUrl(std::string(base), "segment.ts"),
              "https://cdn.example.com/a/b/segment.ts");
    EXPECT_EQ(M3U8Parser::resolveUrl(std::string(base), "../segment.ts"),
              "https://cdn.example.com/a/segment.ts");
    EXPECT_EQ(M3U8Parser::resolveUrl(std::string(base), "../../segment.ts"),
              "https://cdn.example.com/segment.ts");
    EXPECT_EQ(M3U8Parser::resolveUrl(std::string(base), "./x/../segment.ts?part=1#video"),
              "https://cdn.example.com/a/b/segment.ts?part=1#video");
    EXPECT_EQ(M3U8Parser::resolveUrl(std::string(base), "/segment.ts"),
              "https://cdn.example.com/segment.ts");
    EXPECT_EQ(M3U8Parser::resolveUrl(std::string(base), "//media.example.com/segment.ts"),
              "https://media.example.com/segment.ts");
    EXPECT_EQ(M3U8Parser::resolveUrl(std::string(base), "?token=new"),
              "https://cdn.example.com/a/b/master.m3u8?token=new");
    EXPECT_EQ(M3U8Parser::resolveUrl(std::string(base), "#variant"),
              "https://cdn.example.com/a/b/master.m3u8?token=old#variant");
    EXPECT_EQ(M3U8Parser::resolveUrl(std::string(base), "https://other.example.com/x.ts?x=1"),
              "https://other.example.com/x.ts?x=1");
}

TEST(M3U8ParserTest, ReparseReplacesThePreviousSnapshot) {
    M3U8Parser parser("https://example.com/live/");
    ASSERT_TRUE(parser.parse("#EXTM3U\n#EXTINF:1.0,\nold.ts\n"));
    ASSERT_TRUE(parser.parse("#EXTM3U\n#EXT-X-ENDLIST\n#EXTINF:2.0,\nnew.ts\n"));

    const auto playlist = parser.playlist();
    ASSERT_NE(playlist, nullptr);
    ASSERT_EQ(playlist->segments().size(), 1u);
    EXPECT_EQ(playlist->segments()[0].uri, "https://example.com/live/new.ts");
    EXPECT_FALSE(playlist->isLive());
    EXPECT_DOUBLE_EQ(playlist->totalDuration(), 2.0);
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
