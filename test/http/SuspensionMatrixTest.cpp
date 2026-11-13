// Copyright (c) 2026 Nevermore-. All rights reserved.

#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>

#include "CancellationToken.h"
#include "Executor.h"
#include "IOContext.h"
#include "Task.hpp"

using namespace std::chrono_literals;

using slark::CancellationToken;
using slark::CancelledException;
using slark::Error;
using slark::IOResult;
using slark::SerialExecutor;
using slark::Task;
using namespace slark::http;

namespace {

using Counter = std::shared_ptr<std::atomic<int>>;

Counter makeCounter() {
    return std::make_shared<std::atomic<int>>(0);
}

template <typename T>
bool waitUntilDone(const Task<T>& task, std::chrono::seconds timeout = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!task.isDone() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return task.isDone();
}

template <typename T>
bool endedCancelled(Task<T>& task) {
    try {
        (void)task.result();
        return false;
    } catch (const CancelledException&) {
        return true;
    }
}

// Both ends of a pipe, registered with the event loop and non-blocking.
class RegisteredPipe {
public:
    RegisteredPipe() {
        EXPECT_EQ(::pipe(fds_), 0);
        IOContext::instance().registerSocket(fds_[0]);
        IOContext::instance().registerSocket(fds_[1]);
    }

    ~RegisteredPipe() {
        IOContext::instance().unregisterSocket(fds_[0]);
        IOContext::instance().unregisterSocket(fds_[1]);
        ::close(fds_[0]);
        ::close(fds_[1]);
    }

    RegisteredPipe(const RegisteredPipe&) = delete;
    RegisteredPipe& operator=(const RegisteredPipe&) = delete;

    int readEnd() const {
        return fds_[0];
    }

    int writeEnd() const {
        return fds_[1];
    }

    // Consumes everything currently buffered; returns the number of bytes removed.
    size_t drain() const {
        size_t total = 0;
        char chunk[4096];
        while (true) {
            const ssize_t n = ::read(fds_[0], chunk, sizeof chunk);
            if (n <= 0) {
                break;
            }
            total += static_cast<size_t>(n);
        }
        return total;
    }

    // Fills the pipe until a write would block; returns the number of bytes written.
    size_t fill() const {
        size_t total = 0;
        char chunk[4096];
        std::memset(chunk, 'f', sizeof chunk);
        while (true) {
            const ssize_t n = ::write(fds_[1], chunk, sizeof chunk);
            if (n <= 0) {
                break;
            }
            total += static_cast<size_t>(n);
        }
        return total;
    }

private:
    int fds_[2] = {-1, -1};
};

// Row: FdReadyAwaiter; the frame's own token (bound by the test) governs cancellation.
Task<bool> awaitReadableThenMark(int fd, Counter ran) {
    const bool ready = co_await FdReadyAwaiter(fd, FdReadyAwaiter::Interest::Read);
    ran->fetch_add(1);
    co_return ready;
}

// Row: AsyncRead.
Task<IOResult<size_t>> readOnceThenMark(int fd, char* buffer, Counter ran) {
    auto result = co_await AsyncRead(fd, buffer, 1);
    ran->fetch_add(1);
    co_return result;
}

// Row: AsyncWrite.
Task<IOResult<size_t>> writeOnceThenMark(int fd, const char* buffer, size_t size, Counter ran) {
    auto result = co_await AsyncWrite(fd, buffer, size);
    ran->fetch_add(1);
    co_return result;
}

std::shared_ptr<SerialExecutor> stoppedExecutor() {
    auto executor = std::make_shared<SerialExecutor>();
    executor->requestStop();
    return executor;
}

}  // namespace

// FdReadyAwaiter (readiness wait behind a cancellable token)

TEST(SuspensionMatrixTest, FdReadyPreCancelEndsCancelledWithoutSuspending) {
    RegisteredPipe pipe;
    CancellationToken token;
    token.cancel();
    auto ran = makeCounter();
    auto task = awaitReadableThenMark(pipe.readEnd(), ran);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_TRUE(endedCancelled(task));
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, FdReadyMidCancelEndsCancelled) {
    RegisteredPipe pipe;
    CancellationToken token;
    auto ran = makeCounter();
    auto task = awaitReadableThenMark(pipe.readEnd(), ran);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_TRUE(endedCancelled(task));
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, FdReadyReadinessWakesTheWaitAsReady) {
    RegisteredPipe pipe;
    auto ran = makeCounter();
    auto task = awaitReadableThenMark(pipe.readEnd(), ran);
    task.resume();
    ASSERT_FALSE(task.isDone());

    ASSERT_EQ(::write(pipe.writeEnd(), "r", 1), 1);
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_TRUE(task.result());
    EXPECT_EQ(ran->load(), 1);
}

TEST(SuspensionMatrixTest, FdReadyDestroyedWhileSuspendedIsNotResumed) {
    RegisteredPipe pipe;
    auto ran = makeCounter();
    {
        auto task = awaitReadableThenMark(pipe.readEnd(), ran);
        task.resume();
        ASSERT_FALSE(task.isDone());
    }
    ASSERT_EQ(::write(pipe.writeEnd(), "d", 1), 1);
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, FdReadyUnregisteredReportsNotReadyWithoutThrowing) {
    RegisteredPipe pipe;
    auto ran = makeCounter();
    auto task = awaitReadableThenMark(pipe.readEnd(), ran);
    task.resume();
    ASSERT_FALSE(task.isDone());

    IOContext::instance().unregisterSocket(pipe.readEnd());
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_NO_THROW(EXPECT_FALSE(task.result())) << "an abandoned wait is not ready, not cancelled";
    EXPECT_EQ(ran->load(), 1);
}

TEST(SuspensionMatrixTest, FdReadyRefusedDispatchEndsCancelled) {
    RegisteredPipe pipe;
    auto ran = makeCounter();
    auto task = awaitReadableThenMark(pipe.readEnd(), ran);
    task.setExecutor(stoppedExecutor());
    task.resume();
    ASSERT_FALSE(task.isDone());

    ASSERT_EQ(::write(pipe.writeEnd(), "x", 1), 1);
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_TRUE(endedCancelled(task));
    EXPECT_EQ(ran->load(), 0);
}

// AsyncRead

TEST(SuspensionMatrixTest, ReadPreCancelEndsCancelledWithoutReading) {
    RegisteredPipe pipe;
    ASSERT_EQ(::write(pipe.writeEnd(), "p", 1), 1);
    CancellationToken token;
    token.cancel();
    char buffer[1] = {};
    auto ran = makeCounter();
    auto task = readOnceThenMark(pipe.readEnd(), buffer, ran);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_TRUE(task.isDone());
    EXPECT_TRUE(endedCancelled(task));
    EXPECT_EQ(pipe.drain(), 1u) << "a cancelled read must not consume the data";
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, ReadMidCancelEndsCancelledAndLeavesLaterDataUnread) {
    RegisteredPipe pipe;
    CancellationToken token;
    char buffer[1] = {};
    auto ran = makeCounter();
    auto task = readOnceThenMark(pipe.readEnd(), buffer, ran);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_TRUE(endedCancelled(task));
    ASSERT_EQ(::write(pipe.writeEnd(), "m", 1), 1);
    EXPECT_EQ(pipe.drain(), 1u);
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, ReadAlreadyReadyCompletesWithoutSuspending) {
    RegisteredPipe pipe;
    ASSERT_EQ(::write(pipe.writeEnd(), "a", 1), 1);
    char buffer[1] = {};
    auto ran = makeCounter();
    auto task = readOnceThenMark(pipe.readEnd(), buffer, ran);
    task.resume();
    ASSERT_TRUE(task.isDone());
    auto result = task.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1u);
    EXPECT_EQ(buffer[0], 'a');
    EXPECT_EQ(ran->load(), 1);
}

TEST(SuspensionMatrixTest, ReadWokenByReadinessRetriesAndReturnsTheData) {
    RegisteredPipe pipe;
    char buffer[1] = {};
    auto ran = makeCounter();
    auto task = readOnceThenMark(pipe.readEnd(), buffer, ran);
    task.resume();
    ASSERT_FALSE(task.isDone());

    ASSERT_EQ(::write(pipe.writeEnd(), "y", 1), 1);
    ASSERT_TRUE(waitUntilDone(task));
    auto result = task.result();
    ASSERT_TRUE(result.has_value()) << "readiness must retry the read, not report an empty success";
    EXPECT_EQ(*result, 1u);
    EXPECT_EQ(buffer[0], 'y');
    EXPECT_EQ(ran->load(), 1);
}

TEST(SuspensionMatrixTest, ReadDestroyedWhileSuspendedLeavesLaterDataUnread) {
    RegisteredPipe pipe;
    char buffer[1] = {};
    auto ran = makeCounter();
    {
        auto task = readOnceThenMark(pipe.readEnd(), buffer, ran);
        task.resume();
        ASSERT_FALSE(task.isDone());
    }
    ASSERT_EQ(::write(pipe.writeEnd(), "z", 1), 1);
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(pipe.drain(), 1u) << "a destroyed read must not consume the data";
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, ReadUnregisteredReportsSocketClosed) {
    RegisteredPipe pipe;
    char buffer[1] = {};
    auto ran = makeCounter();
    auto task = readOnceThenMark(pipe.readEnd(), buffer, ran);
    task.resume();
    ASSERT_FALSE(task.isDone());

    IOContext::instance().unregisterSocket(pipe.readEnd());
    ASSERT_TRUE(waitUntilDone(task));
    auto result = task.result();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::SocketClosed)
        << "an abandoned read must not be reported as an empty success";
    EXPECT_EQ(ran->load(), 1);
}

TEST(SuspensionMatrixTest, ReadRefusedDispatchEndsCancelledAndLeavesDataUnread) {
    RegisteredPipe pipe;
    char buffer[1] = {};
    auto ran = makeCounter();
    auto task = readOnceThenMark(pipe.readEnd(), buffer, ran);
    task.setExecutor(stoppedExecutor());
    task.resume();
    ASSERT_FALSE(task.isDone());

    ASSERT_EQ(::write(pipe.writeEnd(), "r", 1), 1);
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_TRUE(endedCancelled(task));
    EXPECT_EQ(pipe.drain(), 1u) << "a refused resume must not perform the read";
    EXPECT_EQ(ran->load(), 0);
}

// AsyncWrite

TEST(SuspensionMatrixTest, WriteAlreadyWritableCompletesWithTheCount) {
    RegisteredPipe pipe;
    auto ran = makeCounter();
    auto task = writeOnceThenMark(pipe.writeEnd(), "w", 1, ran);
    task.resume();
    ASSERT_TRUE(task.isDone());
    auto result = task.result();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 1u);
    EXPECT_EQ(pipe.drain(), 1u);
    EXPECT_EQ(ran->load(), 1);
}

TEST(SuspensionMatrixTest, WriteWokenByWritabilityRetriesAndReturnsTheCount) {
    RegisteredPipe pipe;
    pipe.fill();
    auto ran = makeCounter();
    auto task = writeOnceThenMark(pipe.writeEnd(), "w", 1, ran);
    task.resume();
    ASSERT_FALSE(task.isDone()) << "a full pipe must suspend the write";

    pipe.drain();
    ASSERT_TRUE(waitUntilDone(task));
    auto result = task.result();
    ASSERT_TRUE(result.has_value()) << "writability must retry the write";
    EXPECT_EQ(*result, 1u);
    EXPECT_EQ(ran->load(), 1);
}

TEST(SuspensionMatrixTest, WriteMidCancelEndsCancelled) {
    RegisteredPipe pipe;
    pipe.fill();
    CancellationToken token;
    auto ran = makeCounter();
    auto task = writeOnceThenMark(pipe.writeEnd(), "w", 1, ran);
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(task.isDone());
    EXPECT_TRUE(endedCancelled(task));
    EXPECT_EQ(ran->load(), 0);
}

TEST(SuspensionMatrixTest, WriteUnregisteredReportsSocketClosed) {
    RegisteredPipe pipe;
    pipe.fill();
    auto ran = makeCounter();
    auto task = writeOnceThenMark(pipe.writeEnd(), "w", 1, ran);
    task.resume();
    ASSERT_FALSE(task.isDone());

    IOContext::instance().unregisterSocket(pipe.writeEnd());
    ASSERT_TRUE(waitUntilDone(task));
    auto result = task.result();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Error::SocketClosed);
    EXPECT_EQ(ran->load(), 1);
}

// Row: FdReadyAwaiter with a deadline; both sources signal the same gate.
Task<bool> awaitReadableWithin(int fd, Counter ran, std::chrono::milliseconds timeout) {
    const bool ready = co_await FdReadyAwaiter(fd, FdReadyAwaiter::Interest::Read, timeout);
    ran->fetch_add(1);
    co_return ready;
}

TEST(SuspensionMatrixTest, FdReadyDeadlineEndsTheWaitAsNotReady) {
    RegisteredPipe pipe;
    auto ran = makeCounter();
    auto task = awaitReadableWithin(pipe.readEnd(), ran, 30ms);
    task.resume();
    ASSERT_TRUE(waitUntilDone(task)) << "an elapsed deadline must end the wait";
    EXPECT_NO_THROW(EXPECT_FALSE(task.result())) << "a deadline is not a cancellation";
    EXPECT_EQ(ran->load(), 1);
}

TEST(SuspensionMatrixTest, ReadinessBeforeDeadlineCancelsTheDeadline) {
    RegisteredPipe pipe;
    auto ran = makeCounter();
    auto task = awaitReadableWithin(pipe.readEnd(), ran, 60ms);
    task.resume();
    ASSERT_FALSE(task.isDone());
    ASSERT_EQ(::write(pipe.writeEnd(), "r", 1), 1);
    ASSERT_TRUE(waitUntilDone(task));
    EXPECT_TRUE(task.result());

    std::this_thread::sleep_for(120ms);
    EXPECT_EQ(ran->load(), 1) << "the deadline of a finished wait must not resume the frame again";
}
