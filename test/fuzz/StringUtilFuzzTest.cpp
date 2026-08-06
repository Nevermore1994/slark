#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "StringUtil.h"

using namespace slark;

TEST(StringUtilFuzzTest, RandomStringsDoNotCrash) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> charDist(0, 127);

    for (int i = 0; i < 1000; ++i) {
        std::uniform_int_distribution<size_t> lenDist(0, 512);
        size_t len = lenDist(rng);
        std::string input;
        input.reserve(len);
        for (size_t j = 0; j < len; ++j) {
            input.push_back(charDist(rng));
        }
        EXPECT_NO_THROW({
            StringUtil::split(input, ",");
            StringUtil::split(input, "\n");
            StringUtil::split(input, "::");
            StringUtil::removeSpace(input);
            StringUtil::toLower(input);
            StringUtil::startsWith(input, "http");
            StringUtil::endsWith(input, ".com");
            StringUtil::contains(input, "test");
            StringUtil::extractHostFromUri(input);
            StringUtil::toSafeLogString(input);
        });
    }
}

TEST(StringUtilFuzzTest, HighAsciiAndBinary) {
    std::mt19937 rng(123);
    std::uniform_int_distribution<uint8_t> byteDist;

    for (int i = 0; i < 500; ++i) {
        std::uniform_int_distribution<size_t> lenDist(0, 256);
        size_t len = lenDist(rng);
        std::string input;
        input.reserve(len);
        for (size_t j = 0; j < len; ++j) {
            input.push_back(static_cast<char>(byteDist(rng)));
        }
        EXPECT_NO_THROW({
            StringUtil::toSafeLogString(input);
            StringUtil::extractHostFromUri(input);
            StringUtil::removeSpace(input);
            StringUtil::toLower(input);
        });
    }
}

TEST(StringUtilFuzzTest, FromCharsEdgeCases) {
    std::vector<std::string> inputs = {
        "",
        "-",
        "+",
        ".",
        "-1",
        "  123",
        "123abc",
        "99999999999999999999",
        "-99999999999999999999",
        "0x",
        "0xGG",
        "1.5.3",
        "NaN",
        "inf",
        "-inf",
        "  ",
        "\t",
        "\n",
        "\x00",
        "\xFF\xFF",
    };
    for (const auto& input : inputs) {
        EXPECT_NO_THROW({
            StringUtil::fromChars<int>(input);
            StringUtil::fromChars<float>(input);
            StringUtil::fromChars<uint64_t>(input);
        });
    }
}

TEST(StringUtilFuzzTest, LongStrings) {
    std::mt19937 rng(42);
    for (int i = 0; i < 50; ++i) {
        std::uniform_int_distribution<size_t> lenDist(0, 65536);
        size_t len = lenDist(rng);
        std::string input(len, 'a');
        for (size_t j = 0; j < len && j < 100; ++j) {
            input[rng() % len] = static_cast<char>(rng() & 0xFF);
        }
        EXPECT_NO_THROW({
            StringUtil::toSafeLogString(input);
            StringUtil::split(input, ",");
            StringUtil::extractHostFromUri(input);
        });
    }
}

TEST(StringUtilFuzzTest, EmptyAndWhitespaceVariants) {
    std::vector<std::string> inputs = {
        "",
        " ",
        "\t",
        "\n",
        "\r\n",
        "  \t  \n  ",
        std::string(100, ' '),
        std::string(100, '\t'),
        std::string(100, '\0'),
    };
    for (const auto& input : inputs) {
        EXPECT_NO_THROW({
            StringUtil::split(input, " ");
            StringUtil::removeSpace(input);
            StringUtil::toLower(input);
            StringUtil::extractHostFromUri(input);
            StringUtil::toSafeLogString(input);
            StringUtil::startsWith(input, "");
            StringUtil::endsWith(input, "");
        });
    }
}
