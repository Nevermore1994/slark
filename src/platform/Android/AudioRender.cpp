//
// Created by Nevermore on 2025/2/25.
//

#include "AudioRender.h"
#include "SlarkNative.h"
#include "AudioPlayerNative.h"
#include "Log.hpp"
#include <cstdio>

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
    : IAudioRender(std::move(audioInfo) ){
    init();
}

void AudioRender::init() noexcept {
    playerId_ = NativeAudioPlayer::create (audioInfo_->sampleRate, audioInfo_->channels);
    if (playerId_.empty()) {
        LogE("create audio player failed.");
        return;
    }
}

void AudioRender::play() noexcept {
    CheckAudioPlayer();
    NativeAudioPlayer::doAction(playerId_, AudioPlayerAction::Play);
}

void AudioRender::pause() noexcept {
    CheckAudioPlayer();
    NativeAudioPlayer::doAction(playerId_, AudioPlayerAction::Pause);
}

void AudioRender::stop() noexcept {
    CheckAudioPlayer();
    NativeAudioPlayer::doAction(playerId_, AudioPlayerAction::Release);
}

void AudioRender::flush() noexcept {
    CheckAudioPlayer();
    NativeAudioPlayer::doAction(playerId_, AudioPlayerAction::Flush);
}

void AudioRender::setVolume(float volume) noexcept {
    CheckAudioPlayer();
    NativeAudioPlayer::setConfig(playerId_, AudioPlayerConfig::Volume, volume);
}

void AudioRender::sendAudioData(DataPtr audioData) noexcept {
    CheckAudioPlayer();
    NativeAudioPlayer::sendAudioData(playerId_, std::move(audioData));
}


AudioRenderManager& AudioRenderManager::shareInstance() noexcept {
    static auto instance_ = std::unique_ptr<AudioRenderManager>(new AudioRenderManager());
    return *instance_;
}

std::shared_ptr<AudioRender> AudioRenderManager::find(const std::string& playerId) const noexcept {
    std::lock_guard lock(mutex_);
    if (players_.contains(playerId)) {
        return players_.at(playerId);
    }
    return nullptr;
}

void AudioRenderManager::add(const std::string& decoderId, std::shared_ptr<AudioRender> decoder) noexcept {
    std::lock_guard lock(mutex_);
    players_[decoderId] = std::move(decoder);
}

void AudioRenderManager::remove(const std::string& playerId) noexcept {
    std::lock_guard lock(mutex_);
    players_.erase(playerId);
}


} // slark