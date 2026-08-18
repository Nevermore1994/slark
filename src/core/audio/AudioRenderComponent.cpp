//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AudioRenderComponent.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "Log.hpp"

namespace slark {

AudioRenderComponent::AudioRenderComponent(std::shared_ptr<AudioInfo> info)
    : audioInfo_(std::move(info)) {
    init();
}

AudioRenderComponent::~AudioRenderComponent() {
    close();
    dataPlane_.reset();
}

void AudioRenderComponent::init() noexcept {
    maxPlayedTimeUs_.store(0, std::memory_order_release);
    offsetTime_ = {};
    clock_.reset();

    if (!audioInfo_) {
        LogE("audio info is nullptr");
        return;
    }
    dataPlane_ = std::make_shared<AudioDataPlane>();

    auto pimpl = createAudioRender(audioInfo_);
    if (!pimpl) {
        LogE("createAudioRender failed");
        return;
    }
    auto dataPlane = dataPlane_;
    pimpl->setProvider([dataPlane = std::move(dataPlane)](uint8_t* data,
                                                          uint32_t size,
                                                          AudioDataFlag& /*flag*/) -> uint32_t {
        AudioDataPlane::AccessScope access(dataPlane.get());
        if (!access) {
            return 0;
        }
        uint32_t read = size;
        if (!dataPlane->buffer.readExact(data, size)) {
            read = 0;
            if (dataPlane->decoderCompleted.load(std::memory_order_acquire)) {
                read = dataPlane->buffer.readUpTo(data, size);
            }
            if (read == 0) {
                return 0;
            }
            std::fill_n(data + read, size - read, 0);
        }
        dataPlane->renderedDataLength.fetch_add(read, std::memory_order_relaxed);
        return size;
    });
    pimpl_.reset(std::move(pimpl));
    LogI("init success");
}

void AudioRenderComponent::resetDataPlane(std::optional<uint64_t> offsetTimeUs,
                                           bool resetDecoderCompleted,
                                           bool reopen) noexcept {
    if (!dataPlane_) {
        return;
    }
    dataPlane_->closeAndWait();
    dataPlane_->buffer.reset();
    dataPlane_->renderedDataLength.store(0, std::memory_order_release);
    if (offsetTimeUs) {
        dataPlane_->offsetTimeUs.store(*offsetTimeUs, std::memory_order_release);
    }
    if (resetDecoderCompleted) {
        dataPlane_->decoderCompleted.store(false, std::memory_order_release);
    }
    if (reopen) {
        dataPlane_->open();
    }
}

bool AudioRenderComponent::send(AVFrameRefPtr frame) noexcept {
    return process(frame);
}

bool AudioRenderComponent::process(AVFrameRefPtr frame) noexcept {
    if (!frame || !frame->data || frame->data->empty()) {
        LogE("frame data is nullptr");
        return false;
    }
    if (!dataPlane_) {
        return false;
    }
    AudioDataPlane::AccessScope access(dataPlane_.get());
    return access && dataPlane_->buffer.writeExact(
                         frame->data->data(), static_cast<uint32_t>(frame->data->length()));
}

void AudioRenderComponent::reset() noexcept {
    auto impl = pimpl_.load();
    if (impl) {
        impl->flush();
    }
    resetDataPlane(0, true, impl != nullptr);
    if (impl) {
        impl->reset();
    }
    maxPlayedTimeUs_.store(0, std::memory_order_release);
    offsetTime_ = {};
    clock_.reset();
}

std::shared_ptr<AudioInfo> AudioRenderComponent::audioInfo() const noexcept {
    LogA(audioInfo_ != nullptr, "audio render info is nullptr");
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

void AudioRenderComponent::close() noexcept {
    auto pimpl = pimpl_.swap(nullptr);
    if (!pimpl) {
        if (dataPlane_) {
            dataPlane_->closeAndWait();
        }
        audioTimeUpdater_.reset();
        renderEndCallback_ = {};
        return;
    }
    pimpl->stop();
    if (dataPlane_) {
        dataPlane_->closeAndWait();
    }
    pimpl->setProvider(RequestAudioDataFunc{});
    audioTimeUpdater_.reset();
    renderEndCallback_ = {};
}

void AudioRenderComponent::setVolume(float volume) noexcept {
    if (auto pimpl = pimpl_.load()) {
        pimpl->setVolume(volume);
    } else {
        LogE("audio render is nullptr.");
    }
}

void AudioRenderComponent::flush() noexcept {
    auto pimpl = pimpl_.load();
    if (pimpl) {
        pimpl->flush();
    } else {
        LogE("audio render is nullptr.");
    }
    resetDataPlane(std::nullopt, true, pimpl != nullptr);
}

void AudioRenderComponent::seek(double time) noexcept {
    auto pimpl = pimpl_.load();
    if (pimpl) {
        pimpl->flush();
    } else {
        LogE("audio render is nullptr.");
    }
    const auto target = Time::TimePoint::fromSeconds(time);
    resetDataPlane(target.point(), false, pimpl != nullptr);
    maxPlayedTimeUs_.store(target.point(), std::memory_order_release);
    offsetTime_ = target;
    clock_.setTime(offsetTime_);
}

void AudioRenderComponent::updateAudioTime() noexcept {
    if (!dataPlane_ || !audioInfo_) {
        return;
    }
    const auto rendered = dataPlane_->renderedDataLength.load(std::memory_order_acquire);
    const auto offset = Time::TimePoint(
        dataPlane_->offsetTimeUs.load(std::memory_order_acquire));
    clock_.setTime(offset + audioInfo_->dataLen2TimePoint(rendered));
    if (auto updater = audioTimeUpdater_.load()) {
        (*updater)(playedTime());
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
    while (result.point() > prev && !maxPlayedTimeUs_.compare_exchange_weak(
                                        prev, result.point(), std::memory_order_acq_rel)) {
    }
    auto finalTime = std::max(result.point(), prev);
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

}  // namespace slark
