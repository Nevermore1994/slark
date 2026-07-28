#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "M3U8Parser.h"

using namespace slark;

// 随机无效 M3U8 输入不崩溃
TEST(M3U8ParserFuzzTest, InvalidInputDoesNotCrash) {
    M3U8Parser parser("https://example.com");
    std::vector<std::string> invalidInputs = {
        "",
        "\n",
        "#EXTM3U",
        "#EXTINF:abc,\nsegment.ts",
        "#EXTINF:-1,\nsegment.ts",
        "#EXTINF:999999,\nsegment.ts",
        std::string(100000, 'A'),
        std::string(100000, '#'),
        "#EXT-X-STREAM-INF:\n",
        "#EXTINF:10.0,\n" + std::string(10000, '/') + ".ts",
    };

    for (const auto& input : invalidInputs) {
        EXPECT_NO_THROW(parser.parse(input));
        parser.reset();
    }
}

// 随机截断的 M3U8
TEST(M3U8ParserFuzzTest, TruncatedInput) {
    M3U8Parser parser("https://example.com");
    std::string base = "#EXTM3U\n#EXTINF:10.0,\nsegment.ts\n#EXT-X-ENDLIST\n";

    std::mt19937 rng(42);
    for (int i = 0; i < 20; ++i) {
        size_t len = rng() % base.size();
        EXPECT_NO_THROW(parser.parse(base.substr(0, len)));
        parser.reset();
    }
}

// 大量 segment 不崩溃
TEST(M3U8ParserFuzzTest, ManySegments) {
    M3U8Parser parser("https://example.com");
    std::string content = "#EXTM3U\n";
    for (int i = 0; i < 1000; ++i) {
        content += "#EXTINF:10.0,\nsegment" + std::to_string(i) + ".ts\n";
    }
    content += "#EXT-X-ENDLIST\n";
    EXPECT_TRUE(parser.parse(content));
    EXPECT_GE(parser.playlist()->segments().size(), 1000u);
}

// 混合 master 和 media 标签
TEST(M3U8ParserFuzzTest, MixedMasterAndMedia) {
    M3U8Parser parser("https://example.com");
    std::string content = "#EXTM3U\n#EXTINF:10.0,\nsegment.ts\n"
                          "#EXT-X-STREAM-INF:BANDWIDTH=1280000\nvariant.m3u8\n";
    EXPECT_NO_THROW(parser.parse(content));
}

// URL 解析边界
TEST(M3U8ParserFuzzTest, EdgeCaseUrls) {
    M3U8Parser parser("https://example.com/live/");
    std::vector<std::string> urls = {
        "http://other.com/stream.m3u8", "//relative-host.com/stream.m3u8",
        "/absolute/path/stream.m3u8",   "../parent/stream.m3u8",
        "./current/stream.m3u8",        "",
    };
    for (const auto& input : urls) {
        std::string playlist = "#EXTM3U\n#EXTINF:10.0,\n" + input + "\n#EXT-X-ENDLIST\n";
        EXPECT_NO_THROW(parser.parse(playlist));
        parser.reset();
    }
}
