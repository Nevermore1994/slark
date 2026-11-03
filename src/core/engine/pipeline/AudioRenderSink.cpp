//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "AudioRenderSink.h"

#include <algorithm>
#include <mutex>
#include <utility>

namespace slark::engine {
namespace {

class PlatformAudioRenderFactory {
 public:
    static PlatformAudioRenderFactory& shareInstance() noexcept {
        // Never destroyed: threads that outlive main() may still call the accessors.
        static PlatformAudioRenderFactory* instance = new PlatformAudioRenderFactory();
        return *instance;
    }

    void set(AudioRenderSink::RenderFactory factory) noexcept {
        std::lock_guard lock(mutex_);
        factory_ = std::move(factory);
    }

    AudioRenderSink::RenderFactory get() const noexcept {
        std::lock_guard lock(mutex_);
        return factory_;
    }

 private:
    mutable std::mutex mutex_;
    AudioRenderSink::RenderFactory factory_;
};

}  // namespace

void setPlatformAudioRenderFactory(AudioRenderSink::RenderFactory factory) noexcept {
    PlatformAudioRenderFactory::shareInstance().set(std::move(factory));
}

AudioRenderSink::RenderFactory platformAudioRenderFactory() noexcept {
    return PlatformAudioRenderFactory::shareInstance().get();
}

AudioRenderSink::AudioRenderSink(RenderFactory factory, double prefillSec)
    : factory_(std::move(factory)), prefillSec_(prefillSec) {}

AudioRenderSink::~AudioRenderSink() {
    if (!render_) {
        return;
    }
    render_->stop();
    plane_->gate.closeAndWait();
    render_->setProvider({});
}

bool AudioRenderSink::open(std::shared_ptr<slark::AudioInfo> info) {
    if (!info || !factory_) {
        return false;
    }
    auto render = factory_(info);
    if (!render) {
        return false;
    }
    info_ = std::move(info);
    plane_ = std::make_shared<DataPlane>();
    render->setProvider(
        [plane = plane_](uint8_t* data, uint32_t size, slark::AudioDataFlag& flag) {
            return provide(*plane, data, size, flag);
        });
    render_ = std::move(render);
    return true;
}

bool AudioRenderSink::write(AVFrameRefPtr frame) {
    if (!plane_ || !frame || !frame->data || frame->data->empty()) {
        return false;
    }
    slark::AccessGate::Scope access(&plane_->gate);
    return access && plane_->buffer.writeExact(frame->data->data(),
                                               static_cast<uint32_t>(frame->data->length()));
}

uint32_t AudioRenderSink::provide(DataPlane& plane,
                                  uint8_t* data,
                                  uint32_t size,
                                  slark::AudioDataFlag& flag) noexcept {
    slark::AccessGate::Scope access(&plane.gate);
    if (!access || size == 0) {
        flag = slark::AudioDataFlag::Error;
        return 0;
    }
    uint32_t read = size;
    if (!plane.buffer.readExact(data, size)) {
        read = plane.decoderEnded.load(std::memory_order_acquire)
                   ? plane.buffer.readUpTo(data, size)
                   : 0;
        if (read == 0) {
            flag = slark::AudioDataFlag::Error;
            return 0;
        }
        std::fill_n(data + read, size - read, 0);
    }
    plane.consumedBytes.fetch_add(read, std::memory_order_relaxed);
    flag = slark::AudioDataFlag::Normal;
    return size;
}

Time::TimePoint AudioRenderSink::position() const {
    if (!plane_) {
        return Time::TimePoint{0};
    }
    const uint64_t consumed =
        info_->dataLen2TimePoint(plane_->consumedBytes.load(std::memory_order_acquire)).point();
    const uint64_t latency = render_->getHardwareLatency().point();
    const uint64_t offset = plane_->offsetUs.load(std::memory_order_acquire);
    const uint64_t position = consumed > latency ? offset + consumed - latency : offset;
    auto previous = plane_->lastPositionUs.load(std::memory_order_acquire);
    while (position > previous && !plane_->lastPositionUs.compare_exchange_weak(
                                      previous, position, std::memory_order_acq_rel)) {
    }
    return Time::TimePoint{plane_->lastPositionUs.load(std::memory_order_acquire)};
}

bool AudioRenderSink::primed() const {
    if (!plane_) {
        return false;
    }
    return info_->dataLen2TimePoint(plane_->buffer.length()).second() >= prefillSec_;
}

void AudioRenderSink::start() {
    if (render_) {
        render_->play();
    }
}

void AudioRenderSink::pause() {
    if (render_) {
        render_->pause();
    }
}

void AudioRenderSink::setVolume(float volume) {
    if (render_) {
        render_->setVolume(volume);
    }
}

void AudioRenderSink::flush(Time::TimePoint offset) {
    if (!render_) {
        return;
    }
    render_->flush();
    plane_->gate.closeAndWait();
    plane_->buffer.reset();
    plane_->consumedBytes.store(0, std::memory_order_release);
    plane_->offsetUs.store(offset.point(), std::memory_order_release);
    plane_->lastPositionUs.store(offset.point(), std::memory_order_release);
    plane_->decoderEnded.store(false, std::memory_order_release);
    plane_->gate.open();
}

void AudioRenderSink::markDecoderEnded() {
    if (plane_) {
        plane_->decoderEnded.store(true, std::memory_order_release);
    }
}

bool AudioRenderSink::drained() const {
    return plane_ && plane_->decoderEnded.load(std::memory_order_acquire) &&
           plane_->buffer.length() == 0;
}

}  // namespace slark::engine
