//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "Command.h"
#include "Executor.h"

namespace slark::engine {

[[nodiscard]] PlayerState toPlayerState(const Snapshot& snapshot) noexcept;

// publish() and notify() are engine-thread only; callbacks never run under a lock.
class Notifier {
 public:
    explicit Notifier(std::string playerId,
                      std::shared_ptr<IExecutor> executor = std::make_shared<InlineExecutor>());

    void addObserver(IPlayerObserverPtr observer);
    void removeObserver(const IPlayerObserverPtr& observer);

    void publish(const Snapshot& snapshot, Time::TimePoint now);
    void notify(const Notification& notification);

    [[nodiscard]] std::optional<Time::TimePoint> cacheReportDue() const;

 private:
    template <typename F>
    void dispatch(F&& call);

    const std::string playerId_;
    const std::shared_ptr<IExecutor> executor_;

    std::mutex mutex_;
    std::vector<IPlayerObserverPtr> observers_;

    PlayerState lastState_ = PlayerState::NotInited;
    bool preparedNotified_ = false;
    std::optional<Time::TimePoint> lastPosition_;
    Time::TimePoint lastBuffered_{};
    std::optional<Time::TimePoint> lastBufferedAt_;
    bool bufferedPending_ = false;
};

}  // namespace slark::engine
