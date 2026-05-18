//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Data.h"

#include "DataView.hpp"
#include "Error.h"

#include <gtest/gtest.h>

using namespace slark;

TEST(Data, release) {
    Data data("test");
    data.reset();
    ASSERT_EQ(data.empty(), true);
    ASSERT_EQ(data.data(), nullptr);
}

TEST(Data, detachData) {
    Data data("test");
    auto dataPtr = data.detachData();
    ASSERT_EQ(data.empty(), true);
    ASSERT_EQ(data.data(), nullptr);
    dataPtr->reset();
    ASSERT_EQ(dataPtr->empty(), true);
    ASSERT_EQ(dataPtr->data(), nullptr);
}

TEST(Data, copy) {
    Data data("test data copy.");
    auto dataPtr = data.copy();
    ASSERT_TRUE(std::ranges::equal(dataPtr->view(), data.view()));
    auto data1 = data;
    ASSERT_TRUE(std::ranges::equal(data1.view(), data.view()));
    auto data2 = std::move(data1);
    ASSERT_TRUE(std::ranges::equal(data2.view(), data.view()));
    ASSERT_EQ(data1.empty(), true);
    Data data3;
    data3 = data2;
    ASSERT_TRUE(std::ranges::equal(data3.view(), data2.view()));
    Data data4;
    data4 = std::move(data3);
    ASSERT_TRUE(std::ranges::equal(data4.view(), data2.view()));
    ASSERT_EQ(data3.data(), nullptr);

    auto p1 = data.copy(0, 4);
    ASSERT_TRUE(std::ranges::equal(p1->view(), toByteSpan("test")));
    auto p2 = data.copy(10, 4);
    ASSERT_TRUE(std::ranges::equal(p2->view(), toByteSpan("copy")));
    auto p3 = data.copy(0, 20);
    ASSERT_TRUE(std::ranges::equal(p3->view(), toByteSpan("test data copy.")));
    auto p4 = data.copy(0, 0);
    ASSERT_TRUE(p4->view().empty());
    auto p5 = data.copy(5, 0);
    ASSERT_TRUE(p5->view().empty());
    auto p6 = data.copy(128, 0);
    ASSERT_TRUE(p6->view().empty());
    auto p7 = data.copy(0, 15);
    ASSERT_TRUE(std::ranges::equal(p7->view(), dataPtr->view()));
    auto p8 = data.copy(10, 15);
    ASSERT_TRUE(std::ranges::equal(p8->view(), toByteSpan("copy.")));
}

TEST(Data, append) {
    {
        Data data("hello");
        data.append(" world!");
        ASSERT_EQ(data.toStringView(), std::string_view("hello world!"));
    }
    using namespace std::string_literals;
    auto str = "This section provides definitions for the specific terminology and the concepts "
               "used when describing the C++ programming language.\n"
               "\n"
               "A C++ program is a sequence of text files (typically header and source files) that "
               "contain declarations. They undergo translation to become an executable program, "
               "which is executed when the C++ implementation calls its main function.\n"
               "\n"
               "Certain words in a C++ program have special meaning, and these are known as "
               "keywords. Others can be used as identifiers. Comments are ignored during "
               "translation. C++ programs also contain literals, the values of characters inside "
               "them are determined by character sets and encodings. Certain characters in the "
               "program have to be represented with escape sequences.\n"
               ""s;
    Data data("");
    data.append(str);
    ASSERT_EQ(data.toStringView(), std::string_view(str));
    ASSERT_EQ(data.length(), str.length());

    Data data1("hello");
    data1.append(" world!");
    auto data2 = data1 + Data{"xxxxx"};
    ASSERT_EQ(data1.toStringView(), std::string_view("hello world!"));
    ASSERT_EQ(data2.toStringView(), std::string_view("hello world!xxxxx"));
}

TEST(Data, reset) {
    Data data("you can use data");
    data.resetData();
    ASSERT_EQ(data.toStringView(), "");
    ASSERT_EQ(data.length(), 0);
}

TEST(Data, safeOperations) {
    Data data("hello world");

    auto safeCopy = data.safeCopy(0, 5);
    ASSERT_TRUE(safeCopy.has_value());
    ASSERT_EQ((*safeCopy)->toStringView(), "hello");

    auto safeCopyInvalid = data.safeCopy(100, 5);
    ASSERT_TRUE(safeCopyInvalid.has_value());
    ASSERT_TRUE((*safeCopyInvalid)->empty());

    auto safeAt = data.safeAt(0);
    ASSERT_TRUE(safeAt.has_value());
    ASSERT_EQ(*safeAt, static_cast<uint8_t>('h'));

    auto safeAtInvalid = data.safeAt(100);
    ASSERT_FALSE(safeAtInvalid.has_value());

    Data appendData;
    auto result = appendData.safeAppend("test");
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(appendData.toStringView(), "test");
}

TEST(Data, factoryMethods) {
    auto data = Data::fromString("hello");
    ASSERT_TRUE(data);
    ASSERT_EQ(data->toStringView(), "hello");

    auto data2 = Data::fromSpan(toByteSpan("world"));
    ASSERT_TRUE(data2);
    ASSERT_EQ(data2->toStringView(), "world");

    std::vector<uint8_t> vec = {'t', 'e', 's', 't'};
    auto data3 = Data::fromVector(vec);
    ASSERT_TRUE(data3);
    ASSERT_EQ(data3->toStringView(), "test");

    auto concat = Data::concat({toByteSpan("hello"), toByteSpan(" "), toByteSpan("world")});
    ASSERT_TRUE(concat);
    ASSERT_EQ(concat->toStringView(), "hello world");
}

TEST(Data, comparison) {
    Data data1("hello");
    Data data2("hello");
    Data data3("world");

    ASSERT_EQ(data1, data2);
    ASSERT_NE(data1, data3);
    ASSERT_LT(data1, data3);
}

TEST(Data, rangeBasedFor) {
    Data data("hello");
    std::string result;
    for (auto byte : data) {
        result += static_cast<char>(byte);
    }
    ASSERT_EQ(result, "hello");
}

TEST(DataView, basicOperations) {
    DataView view("hello world");
    ASSERT_EQ(view.size(), 11u);
    ASSERT_FALSE(view.empty());
    ASSERT_EQ(view[0], static_cast<uint8_t>('h'));

    auto sub = view.subview(0, 5);
    ASSERT_EQ(sub.toStringView(), "hello");

    auto first = view.first(5);
    ASSERT_EQ(first.toStringView(), "hello");

    auto last = view.last(5);
    ASSERT_EQ(last.toStringView(), "world");
}

TEST(DataView, findOperations) {
    DataView view("hello world");

    ASSERT_EQ(view.find(static_cast<uint8_t>('o')), 4u);
    ASSERT_EQ(view.find(static_cast<uint8_t>('z')), DataView::npos);

    ASSERT_TRUE(view.startsWith(DataView("hello")));
    ASSERT_FALSE(view.startsWith(DataView("world")));

    ASSERT_TRUE(view.endsWith(DataView("world")));
    ASSERT_FALSE(view.endsWith(DataView("hello")));

    ASSERT_TRUE(view.contains(DataView("lo wo")));
    ASSERT_FALSE(view.contains(DataView("xyz")));
}

TEST(DataView, numberOperations) {
    uint8_t data[] = {0x12, 0x34, 0x56, 0x78};
    DataView view(data, 4);

    auto numLE = view.toNumberLE<uint32_t>();
    ASSERT_TRUE(numLE.has_value());
    ASSERT_EQ(*numLE, 0x78563412u);

    auto numBE = view.toNumberBE<uint32_t>();
    ASSERT_TRUE(numBE.has_value());
    ASSERT_EQ(*numBE, 0x12345678u);
}

TEST(ErrorTest, errorHandling) {
    auto error = Error::OutOfMemory;
    auto str = toString(error);
    ASSERT_EQ(str, "Out of memory");

    Expected<int> result = std::unexpected(Error::InvalidArgument);
    ASSERT_FALSE(result.has_value());
    ASSERT_EQ(result.error(), Error::InvalidArgument);

    auto success = isSuccess(result);
    ASSERT_FALSE(success);

    auto value = valueOr(result, 42);
    ASSERT_EQ(value, 42);
}

TEST(DataView, literal) {
    using namespace slark;
    auto view = "hello"_view;
    ASSERT_EQ(view.toStringView(), "hello");
}