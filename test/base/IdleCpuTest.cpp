//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <sys/resource.h>

#include <chrono>
#include <filesystem>
#include <thread>

#include "Writer.h"

using namespace slark;
using namespace std::chrono_literals;

namespace {

// Process-wide CPU time (user + system), in microseconds.
int64_t processCpuTimeUs() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const int64_t user = static_cast<int64_t>(usage.ru_utime.tv_sec) * 1'000'000 +
                         usage.ru_utime.tv_usec;
    const int64_t system = static_cast<int64_t>(usage.ru_stime.tv_sec) * 1'000'000 +
                           usage.ru_stime.tv_usec;
    return user + system;
}

}  // namespace

// W9: an idle Writer worker must sleep on the condition variable, not busy-loop.
TEST(IdleCpuTest, IdleWriterDoesNotSpin) {
    const auto path = std::filesystem::temp_directory_path() / "slark_writer_idle";
    Writer writer;
    ASSERT_TRUE(writer.open(path.string()));

    const auto before = processCpuTimeUs();
    std::this_thread::sleep_for(500ms);
    const auto used = processCpuTimeUs() - before;

    writer.close();
    writer.dispose();
    std::error_code error;
    std::filesystem::remove(path, error);

    EXPECT_LE(used, 30'000) << "idle writer consumed " << used << "us of CPU in 500ms";
}
