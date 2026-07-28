#include "FileUtil.h"

#include <gtest/gtest.h>

#include <string>

using namespace slark;

TEST(FileUtilTest, RootPath) {
    auto path = File::rootPath();
    EXPECT_FALSE(path.empty());
}

TEST(FileUtilTest, TmpPath) {
    auto path = File::tmpPath();
    EXPECT_FALSE(path.empty());
}

TEST(FileUtilTest, DirExists) {
    EXPECT_TRUE(File::isDirExist("/tmp"));
    EXPECT_FALSE(File::isDirExist("/nonexistent_path_xyz"));
}

TEST(FileUtilTest, FileExists) {
    EXPECT_FALSE(File::isFileExist("/nonexistent_file_xyz"));
}

TEST(FileUtilTest, FileSize) {
    EXPECT_EQ(File::fileSize("/nonexistent"), 0u);
}

TEST(FileUtilTest, CachePath) {
    auto path = File::cachePath();
    EXPECT_FALSE(path.empty());
}
