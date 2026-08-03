#include "IOContext.h"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Awaitable.hpp"
#include "Task.hpp"

using namespace slark;
using namespace slark::http;

namespace {

Task<void> suspendedOn(std::shared_ptr<CompletionAwaiter<void>> awaiter) {
    co_await *awaiter;
}

}  // namespace

TEST(IOContextTest, SingletonInstance) {
    auto& ctx1 = IOContext::instance();
    auto& ctx2 = IOContext::instance();
    EXPECT_EQ(&ctx1, &ctx2);
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
