//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "File.h"

#include "FileUtil.h"
#include "Writer.h"

#include <gtest/gtest.h>

using namespace slark;

TEST(Writer, open) {
    Writer writer;
    writer.open("test.txt");
    ASSERT_EQ(writer.state(), IOState::Normal);
    writer.write("open test");
    writer.close();
    ASSERT_EQ(writer.state(), IOState::Closed);
}

TEST(Writer, getPath) {
    Writer writer;
    writer.open("test1.txt");
    auto path = writer.path();
    ASSERT_EQ(path, std::string_view("test1.txt"));
    writer.close();
}

TEST(File, isExist) {
    Writer writer1;
    writer1.open("test1.txt");
    Writer writer2;
    writer2.open("test2.txt");
    writer1.close();
    writer2.close();
    ASSERT_EQ(File::isFileExist("test1.txt"), true);
    ASSERT_EQ(File::isFileExist("test2.txt"), true);
    File::deleteFile("test1.txt");
    File::deleteFile("test2.txt");
    ASSERT_EQ(File::isFileExist("test1.txt"), false);
    ASSERT_EQ(File::isFileExist("test2.txt"), false);
}

TEST(FileUtil, create) {
    auto rootPath = File::rootPath();
    auto testRootPath = rootPath + "/testDir";
    auto testPath = testRootPath + "/test1";
    ASSERT_EQ(File::isDirExist(testPath), false);
    ASSERT_EQ(File::createDir(testPath), true);
    ASSERT_EQ(File::removeDir(testRootPath), true);
}

TEST(FileUtil, RenameFile) {
    const std::string kSrc = "rename_src.txt";
    const std::string kDst = "rename_dst.txt";
    File::deleteFile(kSrc);
    File::deleteFile(kDst);

    {
        File::WriteFile wf(kSrc);
        ASSERT_TRUE(wf.open().has_value());
        wf.write(std::string_view("hello"));
        wf.flush();
    }
    ASSERT_TRUE(File::isFileExist(kSrc));
    ASSERT_TRUE(File::renameFile(kSrc, kDst));
    EXPECT_FALSE(File::isFileExist(kSrc));
    EXPECT_TRUE(File::isFileExist(kDst));
    File::deleteFile(kDst);
}

TEST(FileUtil, FileSize) {
    const std::string kPath = "filesize_test.txt";
    File::deleteFile(kPath);

    {
        File::WriteFile wf(kPath);
        ASSERT_TRUE(wf.open().has_value());
        wf.write(std::string_view("12345"));
        wf.flush();
    }
    EXPECT_EQ(File::fileSize(kPath), 5u);
    File::deleteFile(kPath);
}

TEST(FileUtil, CopyFile) {
    const std::string kSrc = "copy_src.txt";
    const std::string kDst = "copy_dst.txt";
    File::deleteFile(kSrc);
    File::deleteFile(kDst);

    {
        File::WriteFile wf(kSrc);
        ASSERT_TRUE(wf.open().has_value());
        wf.write(std::string_view("copy content"));
        wf.flush();
    }
    ASSERT_TRUE(File::copyFile(kSrc, kDst));
    EXPECT_TRUE(File::isFileExist(kDst));
    EXPECT_EQ(File::fileSize(kDst), File::fileSize(kSrc));
    File::deleteFile(kSrc);
    File::deleteFile(kDst);
}

TEST(File, WriteAndRead) {
    using namespace std::chrono_literals;
    const std::string kPath = "write_read_test.txt";
    File::deleteFile(kPath);

    const std::string kContent = "hello slark file test";
    {
        File::WriteFile wf(kPath);
        ASSERT_TRUE(wf.open().has_value());
        auto result = wf.write(std::string_view(kContent));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(*result, kContent.size());
        wf.flush();
    }

    File::ReadFile rf(kPath);
    ASSERT_TRUE(rf.open().has_value());
    auto data = rf.read(kContent.size());
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data->toStringView(), std::string_view(kContent));
    EXPECT_TRUE(rf.readOver());

    File::deleteFile(kPath);
}

TEST(File, ReadByte) {
    const std::string kPath = "readbyte_test.txt";
    File::deleteFile(kPath);

    {
        File::WriteFile wf(kPath);
        ASSERT_TRUE(wf.open().has_value());
        wf.write(std::string_view("ABC"));
        wf.flush();
    }

    File::ReadFile rf(kPath);
    ASSERT_TRUE(rf.open().has_value());

    auto b1 = rf.readByte();
    ASSERT_TRUE(b1.has_value());
    EXPECT_EQ(*b1, static_cast<Byte>('A'));

    auto b2 = rf.readByte();
    ASSERT_TRUE(b2.has_value());
    EXPECT_EQ(*b2, static_cast<Byte>('B'));

    File::deleteFile(kPath);
}

TEST(File, BackFillByte) {
    const std::string kPath = "backfill_test.txt";
    File::deleteFile(kPath);

    {
        File::WriteFile wf(kPath);
        ASSERT_TRUE(wf.open().has_value());
        wf.write(std::string_view("XY"));
        wf.flush();
    }

    File::ReadFile rf(kPath);
    ASSERT_TRUE(rf.open().has_value());

    auto b = rf.readByte();
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(*b, static_cast<Byte>('X'));

    // 回填一个字节，下次读应该读到回填的字节
    rf.backFillByte(static_cast<Byte>('Z'));
    auto b2 = rf.readByte();
    ASSERT_TRUE(b2.has_value());
    EXPECT_EQ(*b2, static_cast<Byte>('Z'));

    File::deleteFile(kPath);
}

TEST(File, SeekAndRead) {
    const std::string kPath = "seek_test.txt";
    File::deleteFile(kPath);

    {
        File::WriteFile wf(kPath);
        ASSERT_TRUE(wf.open().has_value());
        wf.write(std::string_view("0123456789"));
        wf.flush();
    }

    File::ReadFile rf(kPath);
    ASSERT_TRUE(rf.open().has_value());

    // seek 到第 5 个字节
    ASSERT_TRUE(rf.seek(5).has_value());
    auto data = rf.read(3);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data->toStringView(), std::string_view("567"));

    File::deleteFile(kPath);
}

TEST(File, WriteCount) {
    const std::string kPath = "writecount_test.txt";
    File::deleteFile(kPath);

    File::WriteFile wf(kPath);
    ASSERT_TRUE(wf.open().has_value());
    wf.write(std::string_view("a"));
    wf.write(std::string_view("b"));
    wf.write(std::string_view("c"));
    wf.flush();

    EXPECT_EQ(wf.writeCount(), 3u);
    EXPECT_EQ(wf.writeSize(), 3u);

    File::deleteFile(kPath);
}
