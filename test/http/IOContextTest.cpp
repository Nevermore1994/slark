#include "AsyncSocket.h"
#include "IOContext.h"

#include <gtest/gtest.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <future>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "Awaitable.hpp"
#include "CoTimeout.hpp"
#include "Task.hpp"

using namespace slark;
using namespace slark::http;

namespace {

Task<void> suspendedOn(std::shared_ptr<CompletionAwaiter<void>> awaiter) {
    co_await *awaiter;
}

Task<bool> awaitReadable(int fd, CancellationToken token) {
    co_return co_await withCancellation(FdReadyAwaiter(fd, FdReadyAwaiter::Interest::Read), token);
}

Task<bool> awaitReadableRecording(int fd, std::atomic<std::thread::id>* resumedOn) {
    const bool ready = co_await withCancellation(
        FdReadyAwaiter(fd, FdReadyAwaiter::Interest::Read), CancellationToken{});
    resumedOn->store(std::this_thread::get_id());
    co_return ready;
}

bool waitForDone(const Task<bool>& task) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!task.isDone() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return task.isDone();
}

}  // namespace

TEST(IOContextTest, SingletonInstance) {
    auto& ctx1 = IOContext::instance();
    auto& ctx2 = IOContext::instance();
    EXPECT_EQ(&ctx1, &ctx2);
}

TEST(IOContextTest, ConnectErrnoPreservesNetworkMeaning) {
    EXPECT_EQ(mapConnectErrno(ETIMEDOUT), Error::ConnectionTimeout);
    EXPECT_EQ(mapConnectErrno(ECONNREFUSED), Error::ConnectionRefused);
    EXPECT_EQ(mapConnectErrno(ECONNRESET), Error::ConnectionReset);
    EXPECT_EQ(mapConnectErrno(EBADF), Error::InvalidSocket);
    EXPECT_EQ(mapConnectErrno(EIO), Error::SocketError);
}

TEST(IOContextTest, TransportErrnoPreservesWriteMeaning) {
    EXPECT_EQ(mapTransportErrno(EPIPE), Error::BrokenPipe);
    EXPECT_EQ(mapTransportErrno(ECONNRESET), Error::ConnectionReset);
    EXPECT_EQ(mapTransportErrno(EBADF), Error::InvalidSocket);
    EXPECT_EQ(mapTransportErrno(EIO), Error::SocketError);
}

TEST(IOContextTest, TransientSocketErrorsAreRetryable) {
    EXPECT_TRUE(isTransientSocketError(EAGAIN));
    EXPECT_TRUE(isTransientSocketError(EINTR));
    EXPECT_FALSE(isTransientSocketError(EPIPE));
}

TEST(IOContextTest, RegisterAndUnregisterSocket) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    IOContext::instance().registerSocket(fds[0]);
    IOContext::instance().registerSocket(fds[1]);
    EXPECT_NO_THROW(IOContext::instance().unregisterSocket(fds[0]));
    EXPECT_NO_THROW(IOContext::instance().unregisterSocket(fds[1]));
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(IOContextTest, RegisterContinuation) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    IOContext::instance().registerSocket(fds[0]);

    auto awaiter = std::make_shared<CompletionAwaiter<void>>();
    auto task = suspendedOn(awaiter);
    task.resume();
    auto gate = slark::makeResumeGate(task.getHandle());

    auto token = IOContext::instance().registerContinuation(fds[0], FdInterest::Read, gate);
    EXPECT_TRUE(token.valid());
    token.cancel();

    IOContext::instance().unregisterSocket(fds[0]);
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(IOContextTest, CancellingTheTokenWakesAPendingFdReadyWait) {
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    IOContext::instance().registerSocket(fds[0]);

    CancellationToken token;
    auto task = awaitReadable(fds[0], token);
    task.resume();
    ASSERT_FALSE(task.isDone()) << "an empty pipe is not readable, so the wait must be pending";

    token.cancel();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!task.isDone() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(task.isDone()) << "cancelling the token must wake the wait";
    EXPECT_FALSE(task.result()) << "a cancelled wait reports that the fd is not ready";

    IOContext::instance().unregisterSocket(fds[0]);
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(IOContextTest, CancellingTheCoroutineWakesAPendingFdReadyWait) {
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    IOContext::instance().registerSocket(fds[0]);

    CancellationToken token;
    auto task = awaitReadable(fds[0], CancellationToken{});
    task.setCancellationToken(token);
    task.resume();
    ASSERT_FALSE(task.isDone());

    token.cancel();
    ASSERT_TRUE(waitForDone(task)) << "cancelling the coroutine must wake its pending fd wait";
    EXPECT_FALSE(task.result()) << "a wait woken by cancellation is reported as not ready";

    IOContext::instance().unregisterSocket(fds[0]);
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(IOContextTest, TimeoutWakesAPendingFdReadyWait) {
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    IOContext::instance().registerSocket(fds[0]);

    auto timed = slark::coro::with_timeout(awaitReadable(fds[0], CancellationToken{}),
                                           std::chrono::milliseconds(50));
    timed.resume();
    ASSERT_TRUE(waitForDone(timed)) << "with_timeout must not wait for the fd to become readable";
    EXPECT_THROW(timed.result(), TimeoutException);

    IOContext::instance().unregisterSocket(fds[0]);
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(IOContextTest, ReadinessResumesTheCoroutineOnItsBoundExecutor) {
    auto executor = std::make_shared<SerialExecutor>();
    std::promise<std::thread::id> probe;
    auto probed = probe.get_future();
    executor->execute([p = std::move(probe)]() mutable {
        p.set_value(std::this_thread::get_id());
    });
    const auto worker = probed.get();

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    IOContext::instance().registerSocket(fds[0]);

    std::atomic<std::thread::id> resumedOn{};
    auto task = awaitReadableRecording(fds[0], &resumedOn);
    task.setExecutor(executor);
    task.resume();
    ASSERT_FALSE(task.isDone());

    ASSERT_EQ(write(fds[1], "x", 1), 1);
    ASSERT_TRUE(waitForDone(task));
    EXPECT_TRUE(task.result());
    EXPECT_EQ(resumedOn.load(), worker) << "readiness must resume the coroutine on its executor";

    IOContext::instance().unregisterSocket(fds[0]);
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(IOContextTest, UnregisteringTheSocketReportsAPendingWaitAsNotReady) {
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    IOContext::instance().registerSocket(fds[0]);

    auto task = awaitReadable(fds[0], CancellationToken{});
    task.resume();
    ASSERT_FALSE(task.isDone());

    IOContext::instance().unregisterSocket(fds[0]);
    ASSERT_TRUE(waitForDone(task));
    EXPECT_FALSE(task.result()) << "a wait ended by unregistering the fd is not satisfied";

    ::close(fds[0]);
    ::close(fds[1]);
}

namespace {

Task<bool> awaitReadableCounting(int fd, std::atomic<int>* resumes) {
    const bool ready = co_await withCancellation(FdReadyAwaiter(fd, FdReadyAwaiter::Interest::Read),
                                                 CancellationToken{});
    resumes->fetch_add(1);
    co_return ready;
}

}  // namespace

TEST(IOContextTest, ReadinessRacingCancelResumesExactlyOnce) {
    for (int round = 0; round < 50; ++round) {
        int fds[2];
        ASSERT_EQ(pipe(fds), 0);
        IOContext::instance().registerSocket(fds[0]);

        std::atomic<int> resumes{0};
        CancellationToken token;
        auto task = awaitReadableCounting(fds[0], &resumes);
        task.setCancellationToken(token);
        task.resume();
        ASSERT_FALSE(task.isDone()) << "round " << round;

        std::atomic<bool> go{false};
        std::thread writer([&] {
            while (!go.load()) {
                std::this_thread::yield();
            }
            (void)write(fds[1], "x", 1);
        });
        std::thread canceller([&] {
            while (!go.load()) {
                std::this_thread::yield();
            }
            token.cancel();
        });
        go.store(true);
        writer.join();
        canceller.join();

        ASSERT_TRUE(waitForDone(task)) << "round " << round;
        EXPECT_EQ(resumes.load(), 1) << "round " << round;

        IOContext::instance().unregisterSocket(fds[0]);
        ::close(fds[0]);
        ::close(fds[1]);
    }
}

TEST(IOContextTest, SetReadableWritable) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    IOContext::instance().registerSocket(fds[0]);
    EXPECT_NO_THROW(IOContext::instance().setReadable(fds[0], true));
    EXPECT_NO_THROW(IOContext::instance().setWritable(fds[0], true));
    EXPECT_NO_THROW(IOContext::instance().setReadable(fds[0], false));
    EXPECT_NO_THROW(IOContext::instance().setWritable(fds[0], false));
    IOContext::instance().unregisterSocket(fds[0]);
    ::close(fds[0]);
    ::close(fds[1]);
}
