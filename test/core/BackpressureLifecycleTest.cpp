//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

#include "codec/DecoderComponent.h"

using namespace slark;

// Full-lifecycle simulation of the backpressure hysteresis: the queue grows past the
// high watermark while codec output is still being drained, then the consumer drains
// it back below the low watermark. The feeding must pause exactly at the high watermark
// and resume exactly at the low one, without oscillation.
class BackpressureLifecycleTest : public ::testing::Test {
 protected:
    static constexpr int32_t kLow = 2;
    static constexpr int32_t kHigh = 8;

    // Simulates one runLoop iteration: codec output was already drained into the queue,
    // then backpressure is evaluated. Returns whether feeding is paused afterwards.
    bool iterate(int32_t queueSize, bool& backpressurePaused) const noexcept {
        const bool next = shouldPauseFeeding(queueSize, kLow, kHigh, backpressurePaused);
        if (next != backpressurePaused) {
            backpressurePaused = next;
        }
        return backpressurePaused;
    }

    // Deterministic LCG for repeatable high-frequency / weak-network traffic patterns.
    static uint32_t rng(uint32_t& seed) noexcept {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 16;
    }
};

// Randomized traffic: production and consumption rates vary (weak network bursts,
// high-frequency rendering jitter). Invariants: the queue never grows unbounded, the
// paused state is consistent, and feeding eventually resumes once consumption catches up.
TEST_F(BackpressureLifecycleTest, RandomizedTrafficStaysBoundedAndRecovers) {
    uint32_t seed = 0x5eedu;
    int32_t queue = 0;
    bool paused = false;
    int64_t peak = 0;
    int32_t pauseCount = 0;
    int32_t resumeCount = 0;
    constexpr int32_t kIterations = 20000;

    for (int32_t i = 0; i < kIterations; ++i) {
        if (!paused) {
            // Feeding active: codec produces 0..6 frames per iteration (bursty network).
            queue += static_cast<int32_t>(rng(seed) % 7);
        }
        // Consumer drains 0..3 frames (vsync jitter), never more than available.
        const int32_t drain = static_cast<int32_t>(rng(seed) % 4);
        queue = std::max<int32_t>(0, queue - drain);

        const bool before = paused;
        paused = iterate(queue, paused);
        if (paused && !before) {
            ++pauseCount;
        } else if (!paused && before) {
            ++resumeCount;
        }
        peak = std::max<int64_t>(peak, queue);
        ASSERT_GE(queue, 0);
        ASSERT_LT(queue, 1 << 20) << "queue grew unbounded at iteration " << i;
    }

    // Peak is bounded by high watermark plus one worst-case production burst.
    EXPECT_LE(peak, static_cast<int64_t>(kHigh) + 6);
    // Both directions must have actually been exercised by the random traffic.
    EXPECT_GT(pauseCount, 0);
    EXPECT_GT(resumeCount, 0);
}

// High-frequency toggling: the queue hovers right around the watermarks, so pause and
// resume keep firing. The state must never get stuck or drift.
TEST_F(BackpressureLifecycleTest, HighFrequencyTogglingNeverStalls) {
    bool paused = false;
    int32_t queue = 0;
    int32_t totalTransitions = 0;
    bool lastPaused = paused;

    for (int32_t i = 0; i < 100000; ++i) {
        // Alternate between slightly-over-high and slightly-under-low traffic.
        if (i % 2 == 0) {
            queue = kHigh + 1;
        } else {
            queue = kLow - 1;
        }
        paused = iterate(queue, paused);
        if (paused != lastPaused) {
            ++totalTransitions;
            lastPaused = paused;
        }
        // Final state must be self-consistent after the last sample.
    }
    EXPECT_GT(totalTransitions, 0);
    // After a low sample the decoder must be running.
    EXPECT_FALSE(shouldPauseFeeding(kLow - 1, kLow, kHigh, false));
    // After a high sample it must be paused.
    EXPECT_TRUE(shouldPauseFeeding(kHigh + 1, kLow, kHigh, false));
}

// Weak network: long production silence (server stalls) followed by a burst.
// The queue must drain to zero during silence and survive the burst without a stall.
TEST_F(BackpressureLifecycleTest, WeakNetworkSilenceThenBurstRecovers) {
    int32_t queue = 0;
    bool paused = false;

    // Long silence: no production, consumer keeps draining to zero.
    for (int32_t i = 0; i < 100; ++i) {
        queue = std::max<int32_t>(0, queue - 1);
        paused = iterate(queue, paused);
        EXPECT_FALSE(paused) << "must stay running during silence at iteration " << i;
    }
    EXPECT_EQ(queue, 0);

    // Burst: a large batch lands at once (weak-network catch-up).
    queue += 30;
    paused = iterate(queue, paused);
    EXPECT_TRUE(paused) << "large burst must trip the backpressure";

    // Consumer drains the burst; feeding resumes below the low watermark.
    while (paused) {
        queue -= 1;
        ASSERT_GE(queue, 0);
        paused = iterate(queue, paused);
    }
    EXPECT_LE(queue, kLow);
    EXPECT_FALSE(paused);
}

// Extreme watermarks: the smallest possible hysteresis band and the widest one.
TEST_F(BackpressureLifecycleTest, ExtremeWatermarks) {
    // Minimal band low=0, high=1: no dead zone at all.
    EXPECT_FALSE(shouldPauseFeeding(0, 0, 1, false));
    EXPECT_TRUE(shouldPauseFeeding(1, 0, 1, false));
    EXPECT_FALSE(shouldPauseFeeding(0, 0, 1, true));
    EXPECT_TRUE(shouldPauseFeeding(1, 0, 1, true));

    // Very wide band: recovery requires a long drain, but must still terminate.
    constexpr int32_t kVeryLow = 10;
    constexpr int32_t kVeryHigh = 1000000;
    bool paused = shouldPauseFeeding(kVeryHigh, kVeryLow, kVeryHigh, false);
    ASSERT_TRUE(paused);
    // Still paused just above low, resumes exactly at low.
    EXPECT_TRUE(shouldPauseFeeding(kVeryLow + 1, kVeryLow, kVeryHigh, paused));
    EXPECT_FALSE(shouldPauseFeeding(kVeryLow, kVeryLow, kVeryHigh, paused));
}

// Queue size near INT32_MAX must not overflow or misbehave.
TEST_F(BackpressureLifecycleTest, NearIntegerOverflowPending) {
    const int32_t huge = INT32_MAX - 1;
    // Paused at a huge backlog: stays paused (no overflow wrap to a resume).
    EXPECT_TRUE(shouldPauseFeeding(huge, kLow, kHigh, true));
    // Not paused at a huge backlog: pauses immediately.
    EXPECT_TRUE(shouldPauseFeeding(huge, kLow, kHigh, false));
    // INT32_MAX itself is representable and treated consistently.
    EXPECT_TRUE(shouldPauseFeeding(INT32_MAX, kLow, kHigh, false));
}

// Long-run stability: thousands of full pause/resume cycles must not drift the state.
TEST_F(BackpressureLifecycleTest, ManyCyclesStayConsistent) {
    constexpr int32_t kCycles = 2000;
    for (int32_t cycle = 0; cycle < kCycles; ++cycle) {
        // Drain to empty.
        int32_t queue = 0;
        bool paused = false;
        EXPECT_FALSE(shouldPauseFeeding(queue, kLow, kHigh, paused));

        // Grow past high -> pause.
        while (!paused) {
            queue += 1;
            paused = iterate(queue, paused);
        }
        ASSERT_TRUE(paused);

        // Drain below low -> resume.
        while (paused) {
            queue -= 1;
            ASSERT_GE(queue, 0);
            paused = iterate(queue, paused);
        }
        EXPECT_FALSE(paused) << "state drifted after " << cycle << " cycles";
    }
}

TEST_F(BackpressureLifecycleTest, PausesFeedingExactlyAtHighWatermark) {
    int32_t queue = 0;
    bool paused = false;

    // The producer outpaces the consumer (decode is faster than rendering), so the
    // queue grows until the high watermark trips the pause.
    int32_t produced = 0;
    while (!paused) {
        queue += 2;  // codec produces two per iteration
        queue -= 1;  // consumer drains one
        if (queue < 0) {
            queue = 0;
        }
        produced++;
        paused = iterate(queue, paused);
        ASSERT_LT(produced, 1000);
    }

    // Reached the high watermark and paused.
    EXPECT_GE(queue, kHigh);
    EXPECT_TRUE(paused);

    // Once paused, the codec backlog drains to zero and the queue only shrinks with the
    // consumer; it must never grow back above the peak reached at the pause point.
    const int32_t peak = queue;
    int32_t prev = queue;
    for (int i = 0; i < 100 && paused; ++i) {
        queue -= 1;  // consumer keeps draining; no new frames are produced
        if (queue < 0) {
            queue = 0;
        }
        ASSERT_LE(queue, prev);
        prev = queue;
        paused = iterate(queue, paused);
    }
    EXPECT_LE(queue, peak);
}

TEST_F(BackpressureLifecycleTest, ResumesExactlyAtLowWatermark) {
    int32_t queue = kHigh;
    bool paused = true;

    while (paused) {
        queue -= 1;  // consumer drains
        ASSERT_GE(queue, 0);
        paused = iterate(queue, paused);
    }

    // Resumed exactly at the low watermark.
    EXPECT_LE(queue, kLow);
    EXPECT_FALSE(paused);
}

TEST_F(BackpressureLifecycleTest, CodecBacklogBeyondHighWatermarkStaysBounded) {
    int32_t queue = 0;
    bool paused = false;

    // A burst: codec drains 5 frames in one iteration (typical MediaCodec batch).
    while (!paused) {
        queue += 5;
        paused = iterate(queue, paused);
        if (queue > kHigh) {
            // Once past the high watermark, the batch already violated it; the pause
            // must trigger on the very next evaluation and no more output may be added.
            break;
        }
    }
    EXPECT_TRUE(paused);
    const int32_t overshoot = queue - kHigh;
    EXPECT_GE(overshoot, 0);

    // No infinite growth: with feeding paused and codec drained, the queue only shrinks
    // as the consumer consumes.
    int32_t prev = queue;
    for (int i = 0; i < 200; ++i) {
        queue -= 1;
        if (queue < 0) {
            queue = 0;
        }
        paused = iterate(queue, paused);
        ASSERT_LE(queue, prev);  // monotonic decrease
        prev = queue;
        if (!paused) {
            break;
        }
    }
}

TEST_F(BackpressureLifecycleTest, ConsumerStalledKeepsFeedingPaused) {
    int32_t queue = kHigh;
    bool paused = true;

    // Consumer stalls (e.g. player paused): queue never drains, feeding stays paused.
    for (int i = 0; i < 50; ++i) {
        paused = iterate(queue, paused);
        EXPECT_TRUE(paused);
    }

    // Consumer resumes: drains below low watermark -> feeding resumes.
    while (paused) {
        queue -= 1;
        ASSERT_GE(queue, 0);
        paused = iterate(queue, paused);
    }
    EXPECT_FALSE(paused);
}

TEST_F(BackpressureLifecycleTest, OscillationIsImpossibleAroundWatermarks) {
    // After resuming at low, a tiny amount of production must not re-pause until the
    // high watermark is reached again.
    bool paused = false;
    for (int32_t q = kLow + 1; q < kHigh; ++q) {
        paused = iterate(q, paused);
        EXPECT_FALSE(paused) << "must not re-pause at queue=" << q;
    }
    EXPECT_TRUE(iterate(kHigh, paused));
}
