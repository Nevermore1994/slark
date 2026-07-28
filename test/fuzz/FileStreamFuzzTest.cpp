#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

#include "FileStream.h"
#include "IStream.h"

using namespace slark;

// 不实际读写文件，只测试 FileStream 的并发状态机操作
// 注意：open 需要有效文件路径，此处用临时文件
static std::string createTempFile() {
    char tmp[] = "/tmp/slark_fuzz_XXXXXX";
    auto fd = mkstemp(tmp);
    if (fd < 0) {
        return {};
    }
    ::close(fd);
    std::string path(tmp);
    auto* f = std::fopen(path.c_str(), "w");
    if (f) {
        std::vector<char> data(1024 * 16, 'x');
        std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
    }
    return path;
}

TEST(FileStreamFuzzTest, ConcurrentSeekAndPause) {
    auto path = createTempFile();
    FileStream stream;
    std::atomic<bool> running{true};
    std::atomic<int> errorCount{0};

    OpenParams params;
    params.uri = path;
    params.readChunkSize = 1024;

    auto result = stream.open(params);
    ASSERT_TRUE(result.has_value());

    stream.start();

    std::thread pauser([&]() {
        std::mt19937 rng(42);
        for (int i = 0; i < 50; ++i) {
            stream.pause();
            std::this_thread::sleep_for(std::chrono::microseconds(100 + (rng() % 500)));
            stream.start();
            std::this_thread::yield();
        }
        running.store(false);
    });

    std::thread seeker([&]() {
        std::mt19937 rng(123);
        while (running.load()) {
            uint64_t pos = static_cast<uint64_t>(rng() % 8192);
            auto seekResult = stream.seek(pos);
            if (!seekResult.has_value()) {
                errorCount++;
            }
            std::this_thread::yield();
        }
    });

    pauser.join();
    seeker.join();
    stream.close();
    std::remove(path.c_str());
    SUCCEED();
}

TEST(FileStreamFuzzTest, RapidOpenClose) {
    auto path = createTempFile();
    for (int i = 0; i < 100; ++i) {
        FileStream stream;
        OpenParams params;
        params.uri = path;
        params.readChunkSize = 1024;
        auto result = stream.open(params);
        if (result.has_value()) {
            stream.start();
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            stream.close();
        }
    }
    std::remove(path.c_str());
    SUCCEED();
}
