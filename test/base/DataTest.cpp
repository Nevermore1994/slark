//
// Created by Nevermore on 2024/3/27.
// slark DataTest
// Copyright (c) 2024 Nevermore All rights reserved.
//
#include <gtest/gtest.h>
#include "Data.hpp"

using namespace slark;

TEST(Data, release) {
    Data data("test");
    data.reset();
    ASSERT_EQ(data.empty(), true);
    ASSERT_EQ(data.rawData, nullptr);
}

TEST(Data, detachData) {
    Data data("test");
    auto dataPtr = data.detachData();
    ASSERT_EQ(data.empty(), true);
    ASSERT_EQ(data.rawData, nullptr);
    dataPtr->reset();
    ASSERT_EQ(dataPtr->empty(), true);
    ASSERT_EQ(dataPtr->rawData, nullptr);
}

TEST(Data, copy) {
    Data data("test data copy.");
    auto dataPtr = data.copy();
    ASSERT_EQ(dataPtr->view(),data.view());
    auto data1 = data;
    ASSERT_EQ(data1.view(),data.view());
    auto data2 = std::move(data1);
    ASSERT_EQ(data2.view(),data.view());
    ASSERT_EQ(data1.empty(), true);
    Data data3;
    data3 = data2;
    ASSERT_EQ(data3.view(),data2.view());
    Data data4;
    data4 = std::move(data3);
    ASSERT_EQ(data4.view(),data2.view());
    ASSERT_EQ(data3.rawData, nullptr);

    auto p1 = data.copy(0,4);
    ASSERT_EQ(p1->view().view(), std::string_view("test"));
    auto p2 = data.copy(10,4);
    ASSERT_EQ(p2->view().view(), std::string_view("copy"));
    auto p3 = data.copy(0, 20);
    ASSERT_EQ(p3->view().view(), std::string_view("test data copy."));
    auto p4 = data.copy(0, 0);
    ASSERT_EQ(p4->view().view(), std::string_view(""));
    auto p5 = data.copy(5, 0);
    ASSERT_EQ(p5->view().view(), std::string_view(""));
    auto p6 = data.copy(128, 0);
    ASSERT_EQ(p6->view().view(), std::string_view(""));
    auto p7 = data.copy(0, 15);
    ASSERT_EQ(p7->view(), dataPtr->view());
    auto p8 = data.copy(10, 15);
    ASSERT_EQ(p8->view().view(), std::string_view("copy."));
}

TEST(Data, append) {
    {
        Data data("hello");
        data.append(" world!");
        ASSERT_EQ(data.view().view(), std::string_view("hello world!"));
    }
    using namespace std::string_literals;
    auto str = "This section provides definitions for the specific terminology and the concepts used when describing the C++ programming language.\n"
               "\n"
               "A C++ program is a sequence of text files (typically header and source files) that contain declarations. They undergo translation to become an executable program, which is executed when the C++ implementation calls its main function.\n"
               "\n"
               "Certain words in a C++ program have special meaning, and these are known as keywords. Others can be used as identifiers. Comments are ignored during translation. C++ programs also contain literals, the values of characters inside them are determined by character sets and encodings. Certain characters in the program have to be represented with escape sequences.\n"
               ""s;
    Data data("");
    data.append(str);
    ASSERT_EQ(data.view().view(), std::string_view(str));
    ASSERT_EQ(data.length, str.length());

    Data data1("hello");
    data1.append(" world!");
    auto data2 = data1 + Data{"xxxxx"};
    ASSERT_EQ(data1.view().view(), std::string_view("hello world!"));
    ASSERT_EQ(data2.view().view(), std::string_view("hello world!xxxxx"));
}

TEST(Data, reset) {
    using namespace std::string_literals;
    Data data("you can use data");
    data.resetData();
    ASSERT_EQ(data.view().view(), "");
    ASSERT_EQ(data.length, 0);
}