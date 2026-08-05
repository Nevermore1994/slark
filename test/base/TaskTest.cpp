// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "Task.hpp"

#include <gtest/gtest.h>

using namespace slark;

Task<int> simpleIntTask(int value) {
    co_return value;
}

TEST(TaskTest, CreateAndResume) {
    auto task = simpleIntTask(42);
    EXPECT_TRUE(task.isValid());
    EXPECT_FALSE(task.isDone());

    task.resume();

    EXPECT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 42);
}

TEST(TaskTest, VoidTask) {
    auto task = []() -> Task<void> {
        co_return;
    }();
    EXPECT_TRUE(task.isValid());

    task.resume();

    EXPECT_TRUE(task.isDone());
    EXPECT_NO_THROW(task.result());
}

TEST(TaskTest, ResumeIdempotent) {
    auto task = simpleIntTask(99);
    task.resume();
    EXPECT_TRUE(task.isDone());

    task.resume();
    task.resume();
    EXPECT_EQ(task.result(), 99);
}

TEST(TaskTest, ReturnString) {
    auto task = []() -> Task<std::string> {
        co_return "hello";
    }();
    task.resume();
    EXPECT_EQ(task.result(), "hello");
}

TEST(TaskTest, ReturnComplexType) {
    struct Point {
        int x, y;

        bool operator==(const Point& o) const {
            return x == o.x && y == o.y;
        }
    };

    auto task = []() -> Task<Point> {
        co_return Point{10, 20};
    }();
    task.resume();
    auto p = task.result();
    EXPECT_EQ(p.x, 10);
    EXPECT_EQ(p.y, 20);
}

TEST(TaskTest, ExceptionPropagation) {
    auto task = []() -> Task<void> {
        throw std::runtime_error("task error");
        co_return;
    }();

    task.resume();
    EXPECT_TRUE(task.isDone());
    EXPECT_THROW(task.result(), std::runtime_error);
}

TEST(TaskTest, ExceptionInValueTask) {
    auto task = []() -> Task<int> {
        throw std::logic_error("bad logic");
        co_return 0;
    }();

    task.resume();
    EXPECT_THROW(task.result(), std::logic_error);
}

TEST(TaskTest, MoveConstructor) {
    auto task1 = simpleIntTask(42);
    EXPECT_TRUE(task1.isValid());

    auto task2 = std::move(task1);
    EXPECT_FALSE(task1.isValid());
    EXPECT_TRUE(task2.isValid());

    task2.resume();
    EXPECT_EQ(task2.result(), 42);
}

TEST(TaskTest, MoveAssignmentDestroysOldFrame) {
    auto task1 = simpleIntTask(1);
    auto task2 = simpleIntTask(2);

    task2 = std::move(task1);

    EXPECT_FALSE(task1.isValid());
    task2.resume();
    EXPECT_EQ(task2.result(), 1);
}

TEST(TaskTest, MoveToSelfNoOp) {
    auto task = simpleIntTask(42);
    // 故意验证 self-move 的 no-op 语义，经指针间接移动避免编译器 -Wself-move
    auto* self = &task;
    task = std::move(*self);
    EXPECT_TRUE(task.isValid());
    task.resume();
    EXPECT_EQ(task.result(), 42);
}

TEST(TaskTest, DestructorDestroysFrame) {
    auto task = simpleIntTask(42);
}

TEST(TaskTest, DestructorAfterResume) {
    auto task = simpleIntTask(42);
    task.resume();
}

TEST(TaskTest, SymmetricTransfer_SimpleChild) {
    auto task = []() -> Task<int> {
        int v = co_await simpleIntTask(42);
        co_return v + 1;
    }();

    task.resume();
    EXPECT_TRUE(task.isDone());
    EXPECT_EQ(task.result(), 43);
}

TEST(TaskTest, SymmetricTransfer_VoidChild) {
    bool childRan = false;
    auto task = [&childRan]() -> Task<void> {
        co_await [&childRan]() -> Task<void> {
            childRan = true;
            co_return;
        }();
        co_return;
    }();

    task.resume();
    EXPECT_TRUE(task.isDone());
    EXPECT_TRUE(childRan);
}

TEST(TaskTest, SymmetricTransfer_DeepChain) {
    auto task = []() -> Task<int> {
        auto step1 = co_await simpleIntTask(1);
        auto step2 = co_await simpleIntTask(step1 + 10);
        auto step3 = co_await simpleIntTask(step2 + 100);
        co_return step3;
    }();

    task.resume();
    EXPECT_EQ(task.result(), 111);
}

TEST(TaskTest, SymmetricTransfer_ExceptionPropagates) {
    auto task = []() -> Task<int> {
        int v = co_await []() -> Task<int> {
            throw std::runtime_error("child error");
            co_return 0;
        }();
        co_return v;
    }();

    task.resume();
    EXPECT_THROW((void)task.result(), std::runtime_error);
}

TEST(TaskTest, MoveAwaitedTaskHandleIsNull) {
    auto task = simpleIntTask(42);
    auto awaiter = std::move(task).operator co_await();
    EXPECT_FALSE(task.isValid());
    EXPECT_TRUE(task.isDone());
}

TEST(TaskTest, MultipleAwaitsInSequence) {
    auto task = []() -> Task<int> {
        int sum = 0;
        sum += co_await simpleIntTask(10);
        sum += co_await simpleIntTask(20);
        sum += co_await simpleIntTask(30);
        co_return sum;
    }();

    task.resume();
    EXPECT_EQ(task.result(), 60);
}

TEST(TaskTest, TaskWithSuspensionPoint) {
    auto task = []() -> Task<std::string> {
        int v = co_await simpleIntTask(100);
        co_return "value=" + std::to_string(v);
    }();

    task.resume();
    EXPECT_EQ(task.result(), "value=100");
}

TEST(TaskTest, DefaultConstructedTask) {
    Task<int> t;
    EXPECT_FALSE(t.isValid());
    EXPECT_TRUE(t.isDone());
    EXPECT_THROW(t.result(), std::runtime_error);
    EXPECT_NO_THROW(t.resume());
}

TEST(TaskTest, IsDoneBeforeResume) {
    auto task = simpleIntTask(42);
    EXPECT_FALSE(task.isDone());
    task.resume();
    EXPECT_TRUE(task.isDone());
}

TEST(TaskTest, VoidTaskResultNoThrow) {
    auto task = []() -> Task<void> {
        co_return;
    }();
    EXPECT_TRUE(task.isValid());
    task.resume();
    EXPECT_NO_THROW(task.result());
}

TEST(TaskTest, TopLevelTaskNoContinuation) {
    auto task = simpleIntTask(42);
    task.resume();
    EXPECT_TRUE(task.isDone());
}
