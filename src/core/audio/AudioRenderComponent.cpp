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
        clock_.setTime(offsetTime_ + audioInfo_->dataLen2TimePoint(rendered));

        if (auto updater = audioTimeUpdater_.load()) {
            (*updater)(playedTime());
        }
        LogI("request audio data, request size: {}, read: {}", size, read);
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
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::stop() noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->stop();
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
    if (auto pimpl = pimpl_.load()) {
        auto latency = pimpl->getHardwareLatency();
        // latency 为负或超过当前时间时，返回 0 防止下溢
        if (latency.point() <= 0 || t.point() < static_cast<uint64_t>(latency.point())) {
            return Time::TimePoint{0};
        }
        return t - latency;
    }
    return t;
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
