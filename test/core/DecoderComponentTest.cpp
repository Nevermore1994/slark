//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <latch>
#include <memory>
#include <thread>
#include <vector>

#include "Channel.hpp"
#include "DecoderComponent.h"
#include "DecoderConfig.h"

using namespace slark;
using namespace std::chrono_literals;

namespace {

constexpr auto kWatchdogTimeout = std::chrono::seconds(5);

// Test decoder: configurable decode blocking / CodecAgain retry / open failure,
// and records flush/decode threads with an overlap detector (DC2's core assertion).
class StubDecoder final : public IDecoder {
 public:
    StubDecoder() : IDecoder(DecoderType::RAW) {}

    bool open(std::shared_ptr<DecoderConfig> config) noexcept override {
        if (failOpen_) {
            return false;
        }
        config_ = std::move(config);
        isOpen_ = true;
        return true;
    }

    void close() noexcept override {
        // IDecoder::close() internally calls reset() -> flush(); count those separately
        // so flushCount_ only reflects component-driven flushes. No overlap detection:
        // close is terminal and serialized by the caller (post-join or call_once).
        closing_ = true;
        IDecoder::close();
        ++closeCount_;
    }

    void flush() noexcept override {
        enter();
        if (closing_) {
            ++closeFlushCount_;
        } else {
            ++flushCount_;
            flushThread_ = std::this_thread::get_id();
        }
        exit();
    }

    Error decode(AVFrameRefPtr& /*frame*/) noexcept override {
        enter();
        ++decodeCount_;
        decodeThread_ = std::this_thread::get_id();
        if (blockDecode_ && !blockedOnce_.exchange(true)) {
            decodeEntered_->set_value();
            continueDecode_.wait();
        }
        const auto result = alwaysAgain_ ? Error::CodecAgain : Error::Success;
        exit();
        return result;
    }

    void armBlockingDecode(std::promise<void>& entered, std::shared_future<void> cont) {
        blockDecode_ = true;
        decodeEntered_ = &entered;
        continueDecode_ = std::move(cont);
    }

    void setAlwaysAgain(bool enabled) noexcept { alwaysAgain_ = enabled; }
    void setFailOpen(bool enabled) noexcept { failOpen_ = enabled; }

    [[nodiscard]] int flushCount() const noexcept { return flushCount_.load(); }
    [[nodiscard]] int decodeCount() const noexcept { return decodeCount_.load(); }
    [[nodiscard]] int closeCount() const noexcept { return closeCount_.load(); }
    [[nodiscard]] int overlaps() const noexcept { return overlaps_.load(); }
    [[nodiscard]] std::thread::id flushThread() const noexcept { return flushThread_; }
    [[nodiscard]] std::thread::id decodeThread() const noexcept { return decodeThread_; }

 private:
    void enter() noexcept {
        if (active_.fetch_add(1, std::memory_order_acq_rel) != 0) {
            overlaps_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void exit() noexcept { active_.fetch_sub(1, std::memory_order_acq_rel); }

    std::atomic<int> active_{0};
    std::atomic<int> overlaps_{0};
    std::atomic<int> flushCount_{0};
    std::atomic<int> closeFlushCount_{0};
    std::atomic<int> decodeCount_{0};
    std::atomic<int> closeCount_{0};
    std::atomic<bool> closing_{false};
    std::atomic<bool> blockDecode_{false};
    std::atomic<bool> blockedOnce_{false};
    std::atomic<bool> alwaysAgain_{false};
    std::atomic<bool> failOpen_{false};
    std::promise<void>* decodeEntered_ = nullptr;
    std::shared_future<void> continueDecode_;
    std::thread::id flushThread_;
    std::thread::id decodeThread_;
};

DecoderComponentConfig makeConfig() {
    DecoderComponentConfig config;
    config.onData = [](DecoderData) {};
    config.isAudio = true;
    return config;
}

std::shared_ptr<DecoderConfig> makeDecoderConfig() {
    return std::make_shared<DecoderConfig>();
}

struct TestSetup {
    SenderPtr<AVFrameRefPtr> sender;
    std::shared_ptr<DecoderComponent> component;
    std::shared_ptr<StubDecoder> stub;
};

TestSetup makeComponent() {
    auto [sender, receiver] = Channel<AVFrameRefPtr>::create();
    auto component = std::make_shared<DecoderComponent>(std::move(receiver), makeConfig());
    auto stub = std::make_shared<StubDecoder>();
    component->open(stub, makeDecoderConfig());
    return {std::move(sender), std::move(component), std::move(stub)};
}

void sendFrame(const SenderPtr<AVFrameRefPtr>& sender, int64_t pts) {
    auto frame = std::make_shared<AVFrame>();
    frame->pts = pts;
    frame->frameType = AVFrameType::Audio;
    ASSERT_TRUE(sender->send(std::move(frame)).has_value());
}

void waitForDecode(const std::shared_ptr<StubDecoder>& stub, int count) {
    const auto deadline = std::chrono::steady_clock::now() + kWatchdogTimeout;
    while (stub->decodeCount() < count && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    ASSERT_GE(stub->decodeCount(), count);
}

}  // namespace

// DC1: pause (watermark gate closed) followed by flush must not deadlock.
TEST(DecoderComponentTest, FlushDuringPauseDoesNotDeadlock) {
    auto setup = makeComponent();
    setup.component->start();
    setup.component->pause();

    auto flushed = std::async(std::launch::async, [&] { setup.component->flush(); });
    EXPECT_EQ(flushed.wait_for(kWatchdogTimeout), std::future_status::ready);
    EXPECT_EQ(setup.stub->flushCount(), 1);
    EXPECT_EQ(setup.stub->overlaps(), 0);

    setup.component->resume();
    setup.component->stop();
}

// DC2: flush serializes with decode on the decode thread.
TEST(DecoderComponentTest, FlushWaitsForInFlightDecodeAndRunsOnDecodeThread) {
    auto setup = makeComponent();
    setup.component->start();

    sendFrame(setup.sender, 1);
    waitForDecode(setup.stub, 1);
    const auto decodeThread = setup.stub->decodeThread();

    std::promise<void> decodeEntered;
    std::promise<void> continueDecode;
    auto continueFuture = continueDecode.get_future().share();
    setup.stub->armBlockingDecode(decodeEntered, continueFuture);
    auto enteredFuture = decodeEntered.get_future();
    sendFrame(setup.sender, 2);
    ASSERT_EQ(enteredFuture.wait_for(kWatchdogTimeout), std::future_status::ready);

    auto flushed = std::async(std::launch::async, [&] { setup.component->flush(); });
    // The pinned decode must be drained before flush starts: flush cannot return early.
    EXPECT_EQ(flushed.wait_for(200ms), std::future_status::timeout);
    continueDecode.set_value();
    EXPECT_EQ(flushed.wait_for(kWatchdogTimeout), std::future_status::ready);

    EXPECT_EQ(setup.stub->flushThread(), decodeThread) << "flush must run on the decode thread";
    EXPECT_EQ(setup.stub->overlaps(), 0);
    setup.component->stop();
}

// DC3: flush interrupts an endless CodecAgain retry loop quickly.
TEST(DecoderComponentTest, FlushInterruptsDecodeRetryLoop) {
    auto setup = makeComponent();
    setup.stub->setAlwaysAgain(true);
    setup.component->start();

    const auto startedAt = std::chrono::steady_clock::now();
    setup.component->flush();
    const auto elapsed = std::chrono::steady_clock::now() - startedAt;

    // Without the interruption the retry budget alone costs ~1.6s.
    EXPECT_LT(elapsed, 500ms);
    EXPECT_TRUE(setup.component->isRunning());
    setup.component->stop();
}

// DC4: flush/stop interleavings never hang and finalize exactly once.
TEST(DecoderComponentTest, FlushAndStopConvergeWithoutHang) {
    for (int round = 0; round < 200; ++round) {
        auto setup = makeComponent();
        setup.component->start();

        auto flushDone = std::async(std::launch::async, [&] { setup.component->flush(); });
        setup.component->stop();
        EXPECT_EQ(flushDone.wait_for(1s), std::future_status::ready) << "round " << round;
        EXPECT_EQ(setup.stub->closeCount(), 1) << "round " << round;
        EXPECT_EQ(setup.stub->overlaps(), 0) << "round " << round;
    }
}

TEST(DecoderComponentTest, ConcurrentStopFinalizesExactlyOnce) {
    constexpr int kStoppers = 16;
    auto setup = makeComponent();
    setup.component->start();
    setup.component->flush();

    std::latch ready{kStoppers};
    std::vector<std::thread> stoppers;
    for (int i = 0; i < kStoppers; ++i) {
        stoppers.emplace_back([&] {
            ready.count_down();
            ready.wait();
            setup.component->stop();
        });
    }
    for (auto& stopper : stoppers) {
        stopper.join();
    }
    EXPECT_EQ(setup.stub->closeCount(), 1);
    EXPECT_EQ(setup.stub->overlaps(), 0);
}

TEST(DecoderComponentTest, OpenFailureReportsErrorAndStartIsNoop) {
    auto [sender, receiver] = Channel<AVFrameRefPtr>::create();
    DecoderComponentConfig config = makeConfig();
    std::promise<Error> errorSeen;
    auto errorFuture = errorSeen.get_future();
    config.onError = [&errorSeen](Error error) { errorSeen.set_value(error); };
    DecoderComponent component(std::move(receiver), std::move(config));

    auto stub = std::make_shared<StubDecoder>();
    stub->setFailOpen(true);
    component.open(stub, makeDecoderConfig());

    ASSERT_EQ(errorFuture.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(errorFuture.get(), Error::CodecConfigInvalid);
    EXPECT_FALSE(component.isRunning());
    component.start();  // no decoder: must not create a thread
    EXPECT_FALSE(component.isRunning());
    component.stop();
    (void)sender;
}

TEST(DecoderComponentTest, FlushSkippedAfterStopDoesNotTouchDecoder) {
    auto setup = makeComponent();
    setup.component->start();
    setup.component->stop();

    setup.component->flush();
    EXPECT_EQ(setup.stub->flushCount(), 0);
    EXPECT_EQ(setup.stub->closeCount(), 1);
}
