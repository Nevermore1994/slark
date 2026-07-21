#include "Data.h"
#include "File.h"
#include "FileUtil.h"

#include <gtest/gtest.h>
#include <string>

using namespace slark;

TEST(PlatformTest, DataCreateAndResize) {
    auto data = Data::create(1024);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->capacity(), 1024u);
    data->setSize(512);
    EXPECT_EQ(data->size(), 512u);
    data->resize(2048);
    EXPECT_GE(data->capacity(), 2048u);
}

TEST(PlatformTest, DataAppend) {
    auto data = Data::create(0);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->size(), 0u);
    uint8_t bytes[] = {1, 2, 3, 4, 5};
    data->append(ConstByteSpan(bytes, 5));
    EXPECT_EQ(data->size(), 5u);
}

TEST(PlatformTest, TmpDirExists) {
    EXPECT_TRUE(File::isDirExist("/tmp"));
}

TEST(PlatformTest, CachePath) {
    EXPECT_FALSE(File::cachePath().empty());
}
