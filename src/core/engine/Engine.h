//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "BufferPolicy.h"
#include "Command.h"
#include "DebugInfo.h"
#include "EngineState.h"
#include "MediaClock.h"
#include "Notifier.h"
#include "Pipeline.h"
#include "PlaybackFactory.h"
#include "Scheduler.h"
#include "Source.h"

namespace slark::engine {

// All state lives on the scheduler thread; other threads post commands and read snapshots.
class Engine : public std::enable_shared_from_this<Engine> {
 public:
    using TeardownHook = std::function<void()>;

    static std::shared_ptr<Engine> create(Scheduler& scheduler,
                                          Notifier& notifier,
                                          PlaybackFactory factory,
                                          TeardownHook teardownHook = {});

    void post(Command command);
    void close() noexcept;
    // vsync tick from the render side; paces the video pipeline.
    void notifyVsync();
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] DebugInfo debugInfo() const;

 private:
    Engine(Scheduler& scheduler, Notifier& notifier, PlaybackFactory factory,
           TeardownHook teardownHook);

    void apply(Command& command);
    void onPrepare(Prepare& c);
    void onPlay();
    void onPause();
    void onSeek(Seek& c);
    void onSetSpeed(SetSpeed& c);
    void onSetRepeat(SetRepeat& c);
    void onSetVolume(SetVolume& c);
    void onSetMute(SetMute& c);
    void applyVolume();
    void onStop();
    void onRelease();
    bool mayControl();
    void reject();
    void fail(Error error, ErrorDetail detail = {});
    void requestSeek(Time::TimePoint target, SeekMode mode, bool silent);
    void retargetSeek(Time::TimePoint target, SeekMode mode);
    void releasePipelines();
    void teardown();

    void reconcile();
    bool adoptSession();
    bool startSeek();
    bool completeSeek();
    bool retryStuckSeek();
    bool repeatIfEnded();
    PipelineView evaluatePipelines();
    void applyClock();
    void publish();
    void refreshDebugInfo(const Snapshot& s);
    void scheduleWakeup();
    void wake();
    PlaybackFactory::Wake wakeFn();

    Scheduler& scheduler_;
    Notifier& notifier_;
    PlaybackFactory factory_;
    const TeardownHook teardownHook_;

    MediaClock clock_;
    BufferPolicy bufferPolicy_;
    EngineState state_;
    std::unique_ptr<Source> source_;
    // Only the engine thread reads pipelines_ unlocked; notifyVsync runs on the render thread.
    std::vector<std::unique_ptr<Pipeline>> pipelines_;
    mutable std::mutex pipelinesMutex_;

    // Last deriveStatus() result; written only by reconcile().
    Status status_ = Status::Idle;
    SeekEpoch epochCounter_ = kInvalidSeekEpoch;
    SeekEpoch readyEpoch_ = kInvalidSeekEpoch;
    std::atomic<Time::TimePoint> readyTarget_{};
    bool started_ = false;
    bool rebuffering_ = false;
    bool silentSeek_ = false;
    bool firstFrameNotified_ = false;
    Time::TimePoint seekStartedAt_{};
    int seekAttempts_ = 0;
    bool released_ = false;

    std::atomic<bool> wakePending_{false};
    std::atomic<bool> closed_{false};
    mutable std::mutex snapshotMutex_;
    Snapshot snapshot_;

    // Refreshed in publish() on the engine thread; copied out by debugInfo().
    mutable std::atomic<bool> debugEnabled_{false};
    mutable std::mutex debugMutex_;
    DebugInfo debugInfo_;
};

}  // namespace slark::engine
