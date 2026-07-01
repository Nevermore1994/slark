//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AudioRenderComponent.h"

#include "Log.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace slark {

AudioRenderComponent::AudioRenderComponent(std::shared_ptr<AudioInfo> info)
    : audioInfo_(std::move(info)) {
    init();
}

AudioRenderComponent::~AudioRenderComponent() {
    if (auto pimpl = pimpl_.load()) {
        pimpl->stop();
    }
    audioBuffer_.reset();
    pimpl_.reset();
}

void AudioRenderComponent::init() noexcept {
    renderedDataLength_.store(0, std::memory_order_release);
    maxPlayedTimeUs_.store(0, std::memory_order_release);
    offsetTime_ = {};
    clock_.reset();

    if (!audioInfo_) {
        LogE("audio info is nullptr");
        return;
    }
    audioBuffer_ = std::make_unique<SPSCRingBuffer<uint8_t, kAudioBufferSize>>();

    auto pimpl = createAudioRender(audioInfo_);
    if (!pimpl) {
        LogE("createAudioRender failed");
        return;
    }
    pimpl->setProvider([this](uint8_t* data, uint32_t size, AudioDataFlag& /*flag*/) -> uint32_t {
        uint32_t read = size;
        if (!audioBuffer_->readExact(data, size)) {
            read = 0;
            if (decoderCompleted_.load(std::memory_order_acquire)) {
                read = audioBuffer_->readUpTo(data, size);
            }
            if (read == 0) {
                LogI("request audio data failed");
                return 0;
            }
            std::fill_n(data + read, size - read, 0);
        }
        auto rendered = renderedDataLength_.fetch_add(read, std::memory_order_relaxed) + read;
        auto clockTime = offsetTime_ + audioInfo_->dataLen2TimePoint(rendered);
        clock_.setTime(clockTime);
        auto played = playedTime();
        if (auto updater = audioTimeUpdater_.load()) {
            (*updater)(played);
        }
        LogI("request audio data, size:{} read:{} rendered:{} clockTime:{:.6f} played:{:.6f}",
             size, read, rendered, clockTime.second(), played.second());
        return size;
    });
    pimpl_.reset(std::move(pimpl));
    LogI("init success");
}

bool AudioRenderComponent::send(AVFrameRefPtr frame) noexcept {
    return process(frame);
}

bool AudioRenderComponent::process(AVFrameRefPtr frame) noexcept {
    if (!frame || !frame->data || frame->data->empty()) {
        LogE("frame data is nullptr");
        return false;
    }
    return audioBuffer_->writeExact(frame->data->data(),
                                    static_cast<uint32_t>(frame->data->length()));
}

void AudioRenderComponent::reset() noexcept {
    if (audioBuffer_) {
        audioBuffer_->reset();
    }
    renderedDataLength_.store(0, std::memory_order_release);
    maxPlayedTimeUs_.store(0, std::memory_order_release);
    offsetTime_ = {};
    clock_.reset();
    decoderCompleted_.store(false, std::memory_order_release);
    if (auto impl = pimpl_.load()) {
        impl->reset();
    }
}

std::shared_ptr<AudioInfo> AudioRenderComponent::audioInfo() const noexcept {
    SAssert(audioInfo_ != nullptr, "audio render info is nullptr");
    return audioInfo_;
}

void AudioRenderComponent::start() noexcept {
    clock_.start();
    if (auto pimpl = pimpl_.load()) {
        LogD("start audio render");
        pimpl->play();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::pause() noexcept {
    clock_.pause();
    if (auto pimpl = pimpl_.load()) {
        if (pimpl->status() == RenderStatus::Playing) {
            pimpl->pause();
        }
        LogD("pause audio render");
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::stop() noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->stop();
        LogD("stop audio render");
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::setVolume(float volume) noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->setVolume(volume);
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::flush() noexcept {
    if (audioBuffer_) {
        audioBuffer_->reset();
    }
    decoderCompleted_.store(false, std::memory_order_release);
    if (auto pimpl = pimpl_.load()) {
        pimpl->flush();
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::seek(double time) noexcept {
    if (audioBuffer_) {
        audioBuffer_->reset();
    }
    renderedDataLength_.store(0, std::memory_order_release);
    maxPlayedTimeUs_.store(Time::TimePoint::fromSeconds(time).point(),
                           std::memory_order_release);
    offsetTime_ = Time::TimePoint::fromSeconds(time);
    clock_.setTime(offsetTime_);
    if (auto pimpl = pimpl_.load()) {
        pimpl->flush();
    } else {
        LogE("audio render is nullptr.");
    }
}

Time::TimePoint AudioRenderComponent::playedTime() noexcept {
    auto t = clock_.time();
    Time::TimePoint result = t;
    uint64_t latencyUs = 0;
    if (auto pimpl = pimpl_.load()) {
        auto latency = pimpl->getHardwareLatency();
        latencyUs = static_cast<uint64_t>(latency.point());
        if (t.point() >= latencyUs) {
            result = t - latency;
        }
    }
    auto prev = maxPlayedTimeUs_.load(std::memory_order_acquire);
    while (result.point() > prev &&
           !maxPlayedTimeUs_.compare_exchange_weak(prev,
                                                   result.point(),
                                                   std::memory_order_acq_rel)) {
    }
    auto finalTime = std::max(result.point(), prev);
    LogI("playedTime clock:{:.6f} latency:{}us result:{:.6f} prev:{:.6f} final:{:.6f}",
         t.second(), latencyUs, result.second(), Time::TimePoint(prev).second(), Time::TimePoint(finalTime).second());
    return Time::TimePoint(finalTime);
}

void AudioRenderComponent::renderEnd() noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->renderEnd();
    }
    if (renderEndCallback_) {
        renderEndCallback_();
    }
}

bool AudioRenderComponent::isRunning() const noexcept {
    if (auto pimpl = pimpl_.load()) {
        return pimpl->status() == RenderStatus::Playing;
    }
    return false;
}

} // namespace slark
