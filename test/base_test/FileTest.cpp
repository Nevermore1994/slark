//
// Created by Nevermore on 2024/3/28.
// slark WriterTest
// Copyright (c) 2024 Nevermore All rights reserved.
//

#include <gtest/gtest.h>
#include "Writer.hpp"
#include "FileUtil.h"
#include "Reader.hpp"

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

TEST(Reader, getPath) {
    Reader reader;
    reader.open("test1.txt", ReaderSetting());
    auto path = reader.path();
    ASSERT_EQ(path, std::string_view("test1.txt"));
    reader.close();
}

TEST(Reader, open) {
    using namespace std::chrono_literals;
    using namespace std::string_literals;
    FileUtil::deleteFile("test_read.txt");
    auto writer = std::make_unique<Writer>();
    writer->open("test_read.txt");
    auto str = "This section provides definitions for the specific terminology and the concepts used when describing the C++ programming language.\n"
               "A C++ program is a sequence of text files (typically header and source files) that contain declarations. They undergo translation to become an executable program, which is executed when the C++ implementation calls its main function.\n"
               "Certain words in a C++ program have special meaning, and these are known as keywords. Others can be used as identifiers. Comments are ignored during translation. C++ programs also contain literals, the values of characters inside them are determined by character sets and encodings. Certain characters in the program have to be represented with escape sequences.\n"
               ""s;
    writer->write(str);
    writer->close();
    writer.reset();

    Reader reader;
    reader.open("test_read.txt", ReaderSetting([&str](IOData data, IOState state){
        ASSERT_EQ(data.offset, 0);
        ASSERT_EQ(state, IOState::EndOfFile);
        ASSERT_EQ(data.data->view(), std::string_view(str));
    }));
    std::this_thread::sleep_for(2s);
    reader.close();
    ASSERT_EQ(reader.state(), IOState::Closed);
}

TEST(File, isExist) {
    Writer writer1;
    writer1.open("test1.txt");
    Writer writer2;
    writer2.open("test2.txt");
    writer1.close();
    writer2.close();
    ASSERT_EQ(FileUtil::isFileExist("test1.txt"), true);
    ASSERT_EQ(FileUtil::isFileExist("test2.txt"), true);
    FileUtil::deleteFile("test1.txt");
    FileUtil::deleteFile("test2.txt");
    ASSERT_EQ(FileUtil::isFileExist("test1.txt"), false);
    ASSERT_EQ(FileUtil::isFileExist("test2.txt"), false);
}

TEST(FileUtil, create) {
    auto rootPath = FileUtil::rootPath();
    auto testRootPath = rootPath + "/testDir";
    auto testPath = testRootPath + "/test1";
    ASSERT_EQ(FileUtil::isDirExist(testPath), false);
    ASSERT_EQ(FileUtil::createDir(testPath), true);
    ASSERT_EQ(FileUtil::removeDir(testRootPath), true);
}
