//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "Notifier.h"

#include <algorithm>
#include <cstdlib>
#include <type_traits>
#include <utility>
#include <variant>

namespace slark::engine {
namespace {

constexpr int64_t kProgressStepUs = 100'000;
constexpr int64_t kCacheIntervalUs = 500'000;

std::string formatError(const ErrorEvent& event) {
    return std::to_string(static_cast<uint32_t>(event.code)) + "|" +
           std::to_string(event.detail.origin) + "|" + event.detail.host;
}

bool movedEnough(Time::TimePoint now, Time::TimePoint last) noexcept {
    return std::abs((now - last).point()) >= kProgressStepUs;
}

}  // namespace

PlayerState toPlayerState(const Snapshot& s) noexcept {
    switch (s.status) {
        case Status::Idle:
            return PlayerState::NotInited;
        case Status::Opening:
        case Status::Buffering:
        case Status::Seeking:
            return PlayerState::Buffering;
        case Status::Ready:
            return s.wantPlay ? PlayerState::Playing : PlayerState::Pause;
        case Status::Ended:
            return PlayerState::Completed;
        case Status::Failed:
            return PlayerState::Error;
    }
    return PlayerState::Unknown;
}

Notifier::Notifier(std::string playerId, std::shared_ptr<IExecutor> executor)
    : playerId_(std::move(playerId)), executor_(std::move(executor)) {}

void Notifier::addObserver(IPlayerObserverPtr observer) {
    std::lock_guard lock(mutex_);
    observers_.push_back(std::move(observer));
}

void Notifier::removeObserver(const IPlayerObserverPtr& observer) {
    std::lock_guard lock(mutex_);
    std::erase_if(observers_, [&](const IPlayerObserverPtr& o) {
        return !o.owner_before(observer) && !observer.owner_before(o);
    });
}

template <typename F>
void Notifier::dispatch(F&& call) {
    std::vector<IPlayerObserverPtr> targets;
    {
        std::lock_guard lock(mutex_);
        std::erase_if(observers_, [](const IPlayerObserverPtr& o) {
            return o.expired();
        });
        targets = observers_;
    }
    if (targets.empty()) {
        return;
    }
    executor_->execute(
        [targets = std::move(targets), id = playerId_, call = std::forward<F>(call)]() mutable {
            for (const auto& weak : targets) {
                if (auto observer = weak.lock()) {
                    call(*observer, id);
                }
            }
        });
}

void Notifier::publish(const Snapshot& s, Time::TimePoint now) {
    if (s.status == Status::Idle) {
        preparedNotified_ = false;
    } else if (!preparedNotified_ && s.tracksKnown) {
        // Prepared must precede the first frame: the platform starts its render loop on it.
        preparedNotified_ = true;
        lastState_ = PlayerState::Prepared;
        dispatch([](IPlayerObserver& o, std::string_view id) {
            o.notifyPlayerState(id, PlayerState::Prepared);
        });
    }
    const auto state = toPlayerState(s);
    const bool enteredEnded = state == PlayerState::Completed && lastState_ != state;
    if (state != lastState_) {
        lastState_ = state;
        dispatch([state](IPlayerObserver& o, std::string_view id) {
            o.notifyPlayerState(id, state);
        });
    }

    if (s.status == Status::Idle) {
        lastPosition_.reset();
        lastBuffered_ = {};
        lastBufferedAt_.reset();
        bufferedPending_ = false;
        return;
    }

    if (enteredEnded || !lastPosition_ || movedEnough(s.position, *lastPosition_)) {
        const auto position = s.status == Status::Ended ? s.duration : s.position;
        lastPosition_ = position;
        dispatch([sec = position.second()](IPlayerObserver& o, std::string_view id) {
            o.notifyPlayedTime(id, sec);
        });
    }

    bufferedPending_ = s.buffered != lastBuffered_;
    if (bufferedPending_ && (!lastBufferedAt_ || (now - *lastBufferedAt_).point() >= kCacheIntervalUs)) {
        lastBuffered_ = s.buffered;
        lastBufferedAt_ = now;
        bufferedPending_ = false;
        dispatch([sec = s.buffered.second()](IPlayerObserver& o, std::string_view id) {
            o.notifyPlayerEvent(id, PlayerEvent::UpdateCacheTime, std::to_string(sec));
        });
    }
}

std::optional<Time::TimePoint> Notifier::cacheReportDue() const {
    if (!bufferedPending_ || !lastBufferedAt_) {
        return std::nullopt;
    }
    return Time::TimePoint(lastBufferedAt_->point() + kCacheIntervalUs);
}

void Notifier::notify(const Notification& notification) {
    const auto emit = [this](PlayerEvent event, std::string value) {
        dispatch([event, value = std::move(value)](IPlayerObserver& o, std::string_view id) {
            o.notifyPlayerEvent(id, event, value);
        });
    };
    std::visit(
        [&emit](const auto& n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, SeekDone>) {
                emit(PlayerEvent::SeekDone, {});
            } else if constexpr (std::is_same_v<T, PlayEnd>) {
                emit(PlayerEvent::PlayEnd, {});
            } else if constexpr (std::is_same_v<T, FirstFrameRendered>) {
                emit(PlayerEvent::FirstFrameRendered, {});
            } else {
                emit(PlayerEvent::OnError, formatError(n));
            }
        },
        notification);
}

}  // namespace slark::engine
