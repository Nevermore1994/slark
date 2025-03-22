//
// Created by Nevermore on 2025/2/25.
//

#include "AudioRender.h"
#include "SlarkNative.h"
#include "AudioPlayerNative.h"
#include "Log.hpp"

#define CheckAudioPlayer(action) \
    LogI("audio " action);    \
    if (playerId_.empty()) {   \
        LogE("not create audio player"); \
        return; \
    }

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
    CheckAudioPlayer("play")
    NativeAudioPlayer::doAction(playerId_, AudioPlayerAction::Play);
}

void AudioRender::pause() noexcept {
    CheckAudioPlayer("pause")
    NativeAudioPlayer::doAction(playerId_, AudioPlayerAction::Pause);
}

void AudioRender::stop() noexcept {
    CheckAudioPlayer("stop")
    NativeAudioPlayer::doAction(playerId_, AudioPlayerAction::Release);
}

void AudioRender::flush() noexcept {
    CheckAudioPlayer("flush")
    NativeAudioPlayer::doAction(playerId_, AudioPlayerAction::Flush);
}

void AudioRender::setVolume(float volume) noexcept {
    CheckAudioPlayer("volume")
    NativeAudioPlayer::setConfig(playerId_, AudioPlayerConfig::Volume, volume);
}

} // slark