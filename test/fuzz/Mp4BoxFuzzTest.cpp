#include <gtest/gtest.h>

#include <cstring>
#include <random>
#include <vector>

#include "Buffer.hpp"
#include "Data.h"
#include "Mp4Box.h"

using namespace slark;

static std::unique_ptr<Buffer> makeBuffer(const uint8_t* data, size_t size) {
    auto buf = std::make_unique<Buffer>();
    auto d = Data::create(size);
    std::memcpy(d->data(), data, size);
    buf->append(std::move(d));
    return buf;
}

static std::vector<uint8_t> generateRandomBoxData(std::mt19937& rng) {
    std::uniform_int_distribution<uint8_t> byteDist;
    std::uniform_int_distribution<uint32_t> sizeDist(8, 4096);
    uint32_t size = sizeDist(rng);
    std::vector<uint8_t> data(size);
    for (auto& b : data) {
        b = byteDist(rng);
    }
    data[0] = static_cast<uint8_t>((size >> 24) & 0xFF);
    data[1] = static_cast<uint8_t>((size >> 16) & 0xFF);
    data[2] = static_cast<uint8_t>((size >> 8) & 0xFF);
    data[3] = static_cast<uint8_t>(size & 0xFF);
    return data;
}

TEST(Mp4BoxFuzzTest, RandomDataDoesNotCrash) {
    std::mt19937 rng(42);
    for (int i = 0; i < 500; ++i) {
        auto raw = generateRandomBoxData(rng);
        auto buf = makeBuffer(raw.data(), raw.size());
        EXPECT_NO_THROW({
            auto box = Box::createBox(*buf);
            if (box) {
                (void)box->description("");
            }
        });
    }
}

TEST(Mp4BoxFuzzTest, TruncatedData) {
    std::vector<uint8_t> fullData = {
        0x00, 0x00, 0x00, 0x14, 'f',  't',  'y',  'p',  0x69, 0x73,
        0x6F, 0x6D, 0x00, 0x00, 0x02, 0x00, 0x69, 0x73, 0x6F, 0x6D,
    };
    for (size_t len = 0; len <= fullData.size(); ++len) {
        auto buf = makeBuffer(fullData.data(), len);
        EXPECT_NO_THROW(Box::tryParseBoxInfo(*buf));
    }
}

TEST(Mp4BoxFuzzTest, TryParseBoxInfoVariousSizes) {
    std::mt19937 rng(123);
    for (int i = 0; i < 300; ++i) {
        size_t size = (rng() % 64) + 1;
        std::vector<uint8_t> data(size);
        for (auto& b : data) {
            b = static_cast<uint8_t>(rng() & 0xFF);
        }
        auto buf = makeBuffer(data.data(), data.size());
        EXPECT_NO_THROW(Box::tryParseBoxInfo(*buf));
    }
}

TEST(Mp4BoxFuzzTest, InvalidBoxTypes) {
    std::vector<uint32_t> invalidTypes = {
        0x00000000, 0xFFFFFFFF, 0x20202020, 0x0A0A0A0A, 0x01020304,
    };
    for (auto type : invalidTypes) {
        uint8_t data[12] = {0};
        data[0] = 0;
        data[1] = 0;
        data[2] = 0;
        data[3] = 12;
        data[4] = static_cast<uint8_t>((type >> 24) & 0xFF);
        data[5] = static_cast<uint8_t>((type >> 16) & 0xFF);
        data[6] = static_cast<uint8_t>((type >> 8) & 0xFF);
        data[7] = static_cast<uint8_t>(type & 0xFF);
        auto buf = makeBuffer(data, sizeof(data));
        EXPECT_NO_THROW({
            auto box = Box::createBox(*buf);
            (void)box;
        });
    }
}
