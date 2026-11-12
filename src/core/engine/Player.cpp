//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include <algorithm>
#include <limits>
#include <thread>
#include <utility>

#include "AudioRenderSink.h"
#include "ComponentFactories.h"
#include "DnsResolver.h"
#include "Engine.h"
#include "GLContextManager.h"
#include "Log.hpp"
#include "PlaybackFactory.h"
#include "Player.h"
#include "PlayerContext.h"
#include "Random.hpp"
#include "Scheduler.h"
#include "VideoRenderSink.h"

namespace slark {
using namespace slark::engine;

namespace {

PlaybackRange toPlaybackRange(const ResourceItem& item) noexcept {
    if (!item.hasDisplayRange()) {
        return {};
    }
    PlaybackRange range;
    range.startSec = item.displayStart;
    range.endSec = item.displayDuration > 0.0 ? item.displayStart + item.displayDuration
                                              : std::numeric_limits<double>::max();
    return range;
}

}  // namespace

class Player::Impl {
 public:
    std::string playerId;
    std::unique_ptr<PlayerParams> params;
    std::unique_ptr<ThreadScheduler> scheduler;
    std::unique_ptr<Notifier> notifier;
    std::shared_ptr<Engine> engine;
    std::weak_ptr<IVideoRender> videoRender;
    std::vector<IPlayerObserverPtr> observers;
    std::atomic<bool> prepared{false};

    void start() {
        scheduler = std::make_unique<ThreadScheduler>(playerId);
        notifier = std::make_unique<Notifier>(playerId);
        for (const auto& observer : observers) {
            notifier->addObserver(observer);
        }

        ComponentFactories factories;
        if (auto factory = platformAudioRenderFactory()) {
            factories.makeAudioSink = [factory](const std::shared_ptr<AudioInfo>& info)
                -> std::unique_ptr<AudioSink> {
                auto sink = std::make_unique<AudioRenderSink>(factory);
                if (!sink->open(info)) {
                    return nullptr;
                }
                return sink;
            };
        }
        factories.makeVideoSink = [this]() -> std::unique_ptr<VideoSink> {
            if (videoRender.expired()) {
                return nullptr;
            }
            return std::make_unique<VideoRenderSink>(videoRender);
        };
        engine = Engine::create(*scheduler, *notifier,
                                makePlaybackFactory(params->setting,
                                                    PlayerContext{playerId, params->mainGLContext},
                                                    std::move(factories)));
        // The hook captures a weak_ptr to the engine, so it must be installed after it exists.
        attachRenderVsync();
    }

    void attachRenderVsync() {
        if (!engine) {
            // setRenderImpl() may precede prepare(); start() attaches once the engine exists.
            return;
        }
        if (auto render = videoRender.lock()) {
            LogI("attach render vsync hook: playerId={}", playerId);
            const auto weakEngine = std::weak_ptr<Engine>(engine);
            render->setRequestRenderFunc([weakEngine]() -> AVFrameRefPtr {
                if (auto engine = weakEngine.lock()) {
                    engine->notifyVsync();
                }
                return nullptr;
            });
        }
    }

    void post(Command command) {
        if (engine) {
            engine->post(std::move(command));
        }
    }
};

Player::Player(std::unique_ptr<PlayerParams> params) : pimpl_(std::make_shared<Impl>()) {
    pimpl_->playerId = Random::uuid();
    pimpl_->params = std::move(params);
    // Must precede anything async: decoder resource creation needs this context.
    GLContextManager::shareInstance().addMainContext(pimpl_->playerId,
                                                     pimpl_->params->mainGLContext);
    if (const auto& flow = pimpl_->params->setting.flowConfig) {
        preResolve(flow->dns);
    }
}

void Player::preResolve(const DnsConfig& config) {
    if (config.preResolveUrls.empty() || !config.isValid()) {
        return;
    }
    http::DnsResolver::instance().prefetchUrls(config.preResolveUrls,
                                               std::chrono::duration<double>(config.cacheTtlSec));
}

Player::~Player() {
    if (!pimpl_) {
        return;
    }
    auto stack = std::move(pimpl_);
    std::thread([stack = std::move(stack)]() mutable {
        GLContextManager::shareInstance().removeMainContext(stack->playerId);
        if (stack->engine) {
            stack->engine->close();
        }
        if (stack->scheduler) {
            stack->scheduler->stop();
        }
        stack->engine.reset();
        stack->notifier.reset();
        stack->scheduler.reset();
    }).detach();
}

void Player::prepare() noexcept {
    if (!pimpl_ || pimpl_->prepared.exchange(true)) {
        return;
    }
    pimpl_->start();
    // The playback range start is rebased to zero inside the source, so play begins at 0.
    pimpl_->post(Prepare{pimpl_->params->item, pimpl_->params->setting, Time::TimePoint{},
                          toPlaybackRange(pimpl_->params->item)});
}

void Player::play() noexcept {
    if (pimpl_ && pimpl_->prepared.load()) {
        pimpl_->post(Play{});
    }
}

void Player::stop() noexcept {
    if (pimpl_ && pimpl_->prepared.load()) {
        pimpl_->post(Stop{});
    }
}

void Player::pause() noexcept {
    if (pimpl_ && pimpl_->prepared.load()) {
        pimpl_->post(Pause{});
    }
}

void Player::seek(double time, bool isAccurate) noexcept {
    if (pimpl_ && pimpl_->prepared.load()) {
        pimpl_->post(Seek{Time::TimePoint::fromSeconds(time),
                           isAccurate ? SeekMode::Exact : SeekMode::Fast});
    }
}

void Player::setLoop(bool isLoop) {
    if (pimpl_ && pimpl_->prepared.load()) {
        pimpl_->post(SetRepeat{isLoop ? RepeatMode::One : RepeatMode::Off});
    }
}

void Player::setVolume(float volume) {
    if (pimpl_ && pimpl_->prepared.load()) {
        pimpl_->post(SetVolume{std::clamp(volume, 0.0f, 100.0f) / 100.0f});
    }
}

void Player::setMute(bool isMute) {
    if (pimpl_ && pimpl_->prepared.load()) {
        pimpl_->post(SetMute{isMute});
    }
}

void Player::addObserver(IPlayerObserverPtr observer) noexcept {
    // Before prepare() the notifier does not exist yet; buffer and register on start().
    if (pimpl_->notifier) {
        pimpl_->notifier->addObserver(observer);
    }
    pimpl_->observers.push_back(std::move(observer));
}

void Player::removeObserver() noexcept {
    for (const auto& weak : pimpl_->observers) {
        if (pimpl_->notifier) {
            pimpl_->notifier->removeObserver(weak);
        }
    }
    pimpl_->observers.clear();
}

void Player::setRenderImpl(std::weak_ptr<IVideoRender> render) noexcept {
    if (!pimpl_) {
        return;
    }
    pimpl_->videoRender = render;
    // prepare() may already have run without this render handle.
    pimpl_->attachRenderVsync();
}

PlayerParams Player::peek() noexcept {
    if (!pimpl_->params) {
        return {};
    }
    PlayerParams copy;
    copy.item = pimpl_->params->item;
    copy.setting = pimpl_->params->setting;
    copy.mainGLContext = pimpl_->params->mainGLContext;
    return copy;
}

PlayerState Player::state() noexcept {
    if (!pimpl_ || !pimpl_->prepared.load() || !pimpl_->engine) {
        return PlayerState::NotInited;
    }
    return toPlayerState(pimpl_->engine->snapshot());
}

PlayerInfo Player::info() noexcept {
    PlayerInfo result;
    if (!pimpl_ || !pimpl_->engine) {
        return result;
    }
    const auto snapshot = pimpl_->engine->snapshot();
    result.isValid = snapshot.status != Status::Idle && snapshot.status != Status::Failed;
    result.isSeekable = snapshot.isSeekable;
    result.duration = snapshot.duration.second();
    result.hasAudio = snapshot.hasAudio;
    result.hasVideo = snapshot.hasVideo;
    return result;
}

std::string_view Player::playerId() const noexcept {
    return pimpl_ ? std::string_view(pimpl_->playerId) : std::string_view{};
}

double Player::currentPlayedTime() noexcept {
    if (!pimpl_ || !pimpl_->engine) {
        return 0.0;
    }
    return pimpl_->engine->snapshot().position.second();
}

}  // namespace slark
