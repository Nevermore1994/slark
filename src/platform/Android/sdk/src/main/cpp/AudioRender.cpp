//
// Created by Nevermore on 2025/2/25.
//

#include "AudioRender.h"
#include "AudioPlayerNative.h"
#include "Log.hpp"
#include "TimerManager.h"
#include "AudioRenderComponent.h"

#define CheckAudioPlayer() \
    do {                  \
        LogI("audio {}", __FUNCTION__);    \
        if (playerId_.empty()) {   \
            LogE("not create audio player"); \
            return; \
        } \
    } while(false)

namespace slark {


AudioRender::AudioRender(std::shared_ptr<AudioInfo> audioInfo)
    : IAudioRender(std::move(audioInfo)) {
    init();
}

AudioRender::~AudioRender() {
    release();
}

void AudioRender::init() noexcept {
    playerId_ = NativeAudioPlayer::create(
        audioInfo_->sampleRate,
        static_cast<uint8_t>(audioInfo_->channels));
    if (playerId_.empty()) {
        LogE("create audio player failed.");
        return;
    }
    using namespace std::chrono_literals;
    timerId_ = TimerManager::shareInstance().runLoop(
        3000ms,
        [this]() {
            if (status_ == RenderStatus::Stop || status_ == RenderStatus::Unknown) {
                LogI("audio render stop, no need to check played time.");
                return;
            }
            auto renderedTime = Time::TimePoint(NativeAudioPlayer::getPlayedTime(playerId_));
            latency_ = clock_.time() - renderedTime;
        }
    );
    status_ = RenderStatus::Ready;
}

void AudioRender::play() noexcept {
    if (status_ == RenderStatus::Play) {
        return;
    }
    CheckAudioPlayer();
    NativeAudioPlayer::doAction(
        playerId_,
        AudioPlayerAction::Play
    );
    clock_.start();
    status_ = RenderStatus::Play;
}

void AudioRender::pause() noexcept {
    if (status_ == RenderStatus::Pause) {
        return;
    }
    CheckAudioPlayer();
    NativeAudioPlayer::doAction(
        playerId_,
        AudioPlayerAction::Pause
    );
    clock_.pause();
    status_ = RenderStatus::Pause;
}

void AudioRender::release() noexcept {
    if (status_ == RenderStatus::Stop) {
        return;
    }
    CheckAudioPlayer();
    TimerManager::shareInstance().cancel(timerId_);
    NativeAudioPlayer::doAction(
        playerId_,
        AudioPlayerAction::Release
    );
    clock_.pause();
    playerId_.clear();
    status_ = RenderStatus::Stop;
}

void AudioRender::stop() noexcept {
    release();
}

void AudioRender::reset() noexcept {
    clock_.reset();
    renderedDataLength_ = 0;
}

void AudioRender::flush() noexcept {
    CheckAudioPlayer();
    NativeAudioPlayer::doAction(
        playerId_,
        AudioPlayerAction::Flush
    );
}

void AudioRender::setVolume(float volume) noexcept {
    CheckAudioPlayer();
    NativeAudioPlayer::setConfig(
        playerId_,
        AudioPlayerConfig::Volume,
        volume
    );
}

Time::TimePoint AudioRender::playedTime() noexcept {
    return clock_.time() - latency_;
}

std::unique_ptr<IAudioRender> createAudioRender(std::shared_ptr<AudioInfo> audioInfo) {
    return std::make_unique<AudioRender>(audioInfo);
}

} // slark