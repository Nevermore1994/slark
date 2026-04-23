//
// Created by Nevermore on 2025/2/25.
//

#include "AudioRender.h"

#include "AudioPlayerNative.h"
#include "AudioRenderComponent.h"
#include "Log.hpp"

#define CheckAudioPlayer()                   \
    do {                                     \
        LogI("audio {}", __FUNCTION__);      \
        if (playerId_.empty()) {             \
            LogE("not create audio player"); \
            return;                          \
        }                                    \
    } while (false)

namespace slark {

AudioRender::AudioRender(std::shared_ptr<AudioInfo> audioInfo)
    : IAudioRender(std::move(audioInfo)) {
    init();
}

AudioRender::~AudioRender() {
    release();
}

void AudioRender::init() noexcept {
    playerId_ = NativeAudioPlayer::create(audioInfo_->sampleRate,
                                          static_cast<uint8_t>(audioInfo_->channels));
    if (playerId_.empty()) {
        LogE("create audio player failed.");
        status_ = RenderStatus::Error;
        return;
    }
    status_ = RenderStatus::Ready;
}

void AudioRender::play() noexcept {
    if (status_ == RenderStatus::Playing) {
        return;
    }
    CheckAudioPlayer();
    NativeAudioPlayer::doAction(playerId_, AudioPlayerAction::Play);
    clock_.start();
    status_ = RenderStatus::Playing;
}

void AudioRender::pause() noexcept {
    if (status_ == RenderStatus::Pause) {
        return;
    }
    CheckAudioPlayer();
    NativeAudioPlayer::doAction(playerId_, AudioPlayerAction::Pause);
    clock_.pause();
    status_ = RenderStatus::Pause;
}

void AudioRender::release() noexcept {
    if (status_ == RenderStatus::Stop) {
        return;
    }
    CheckAudioPlayer();
    NativeAudioPlayer::doAction(playerId_, AudioPlayerAction::Release);
    clock_.pause();
    AudioRenderManager::shareInstance().remove(playerId_);
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

void AudioRender::seek(double time) noexcept {
    IAudioRender::seek(time);
}

void AudioRender::renderEnd() noexcept {
    if (status_ == RenderStatus::Pause) {
        return;
    }
    CheckAudioPlayer();
    flush();
    NativeAudioPlayer::doAction(playerId_, AudioPlayerAction::Pause);
    status_ = RenderStatus::Pause;
}

void AudioRender::flush() noexcept {
    CheckAudioPlayer();
    NativeAudioPlayer::doAction(playerId_, AudioPlayerAction::Flush);
}

void AudioRender::setVolume(float volume) noexcept {
    CheckAudioPlayer();
    NativeAudioPlayer::setConfig(playerId_, AudioPlayerConfig::Volume, volume);
}

Time::TimePoint AudioRender::playedTime() noexcept {
    // 直接从 Java 层 AudioTrack.getTimestamp() 获取硬件级精确播放时间
    return offsetTime_ + Time::TimePoint(NativeAudioPlayer::getPlayedTime(playerId_));
}

std::shared_ptr<IAudioRender> createAudioRender(const std::shared_ptr<AudioInfo>& audioInfo) {
    auto audioRender = std::make_shared<AudioRender>(audioInfo);
    AudioRenderManager::shareInstance().add(audioRender->playerId(), audioRender);
    return audioRender;
}

} // namespace slark