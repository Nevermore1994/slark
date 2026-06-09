//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Reflection.hpp"

#include <gtest/gtest.h>
#include <string>

using namespace slark;

namespace {

struct TestWidget {
    int32_t value = 42;
    std::string name = "widget";
};

struct TestService {
    double rate = 3.14;
};

struct AnotherWidget {
    int32_t id = 99;
};

SLARK_REGISTER_CLASS(AnotherWidget);

} // namespace

TEST(Reflection, RegisterAndCreate) {
    Reflection::enroll<TestWidget>("TestWidget");
    auto widget = Reflection::shareInstance().create<TestWidget>("TestWidget");
    ASSERT_NE(widget, nullptr);
    EXPECT_EQ(widget->value, 42);
    EXPECT_EQ(widget->name, "widget");
}

TEST(Reflection, CreateUnregistered) {
    auto ptr = Reflection::shareInstance().create<TestWidget>("NonExistentClass");
    EXPECT_EQ(ptr, nullptr);
}

TEST(Reflection, RegisterMultipleTypes) {
    Reflection::enroll<TestWidget>("Widget");
    Reflection::enroll<TestService>("Service");

    auto widget = Reflection::shareInstance().create<TestWidget>("Widget");
    auto service = Reflection::shareInstance().create<TestService>("Service");

    ASSERT_NE(widget, nullptr);
    ASSERT_NE(service, nullptr);
    EXPECT_EQ(widget->value, 42);
    EXPECT_DOUBLE_EQ(service->rate, 3.14);
}

TEST(Reflection, OverwriteRegistration) {
    Reflection::enroll<TestWidget>("Overwrite");
    auto w1 = Reflection::shareInstance().create<TestWidget>("Overwrite");
    ASSERT_NE(w1, nullptr);
    EXPECT_EQ(w1->value, 42);

    Reflection::enroll<TestService>("Overwrite");
    auto w2 = Reflection::shareInstance().create<TestService>("Overwrite");
    ASSERT_NE(w2, nullptr);
    EXPECT_DOUBLE_EQ(w2->rate, 3.14);
}

TEST(Reflection, MacroRegistration) {
    auto widget = Reflection::shareInstance().create<AnotherWidget>("AnotherWidget");
    ASSERT_NE(widget, nullptr);
    EXPECT_EQ(widget->id, 99);
}

TEST(Reflection, CreateReturnsDistinctInstances) {
    Reflection::enroll<TestWidget>("Distinct");
    auto a = Reflection::shareInstance().create<TestWidget>("Distinct");
    auto b = Reflection::shareInstance().create<TestWidget>("Distinct");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a.get(), b.get());
}

TEST(Reflection, SingletonInstance) {
    auto& inst1 = Reflection::shareInstance();
    auto& inst2 = Reflection::shareInstance();
    EXPECT_EQ(&inst1, &inst2);
}

TEST(Reflection, EmptyNameRegistration) {
    Reflection::enroll<TestWidget>("");
    auto widget = Reflection::shareInstance().create<TestWidget>("");
    ASSERT_NE(widget, nullptr);
    EXPECT_EQ(widget->value, 42);
}
