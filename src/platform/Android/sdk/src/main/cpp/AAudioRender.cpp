//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AAudioRender.h"

#include "Log.hpp"

#include <algorithm>
#include <thread>

namespace slark {

AAudioRender::AAudioRender(std::shared_ptr<AudioInfo> audioInfo)
    : IAudioRender(std::move(audioInfo)) {
    initStream();
}

AAudioRender::~AAudioRender() {
    releaseStream();
}

void AAudioRender::initStream() noexcept {
    AAudio_createStreamBuilder(&builder_);
    AAudioStreamBuilder_setDataCallback(builder_, dataCallback, this);
    AAudioStreamBuilder_setErrorCallback(builder_, errorCallback, this);
    AAudioStreamBuilder_setDirection(builder_, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSampleRate(builder_, static_cast<int32_t>(audioInfo_->sampleRate));
    AAudioStreamBuilder_setChannelCount(builder_, static_cast<int32_t>(audioInfo_->channels));
    AAudioStreamBuilder_setFormat(builder_, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setPerformanceMode(builder_, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);

    auto result = AAudioStreamBuilder_openStream(builder_, &stream_);
    if (result != AAUDIO_OK) {
        LogE("AAudio openStream failed: {}", AAudio_convertResultToText(result));
        status_ = RenderStatus::Error;
        return;
    }
    flushBaseFrames_.store(0, std::memory_order_release);
    status_ = RenderStatus::Ready;
}

void AAudioRender::releaseStream() noexcept {
    if (stream_) {
        AAudioStream_close(stream_);
        stream_ = nullptr;
    }
    if (builder_) {
        AAudioStreamBuilder_delete(builder_);
        builder_ = nullptr;
    }
}

aaudio_data_callback_result_t AAudioRender::dataCallback(AAudioStream* /*stream*/,
                                                         void* userData,
                                                         void* audioData,
                                                         int32_t numFrames) {
    auto* render = static_cast<AAudioRender*>(userData);
    auto bytesPerFrame = static_cast<uint32_t>(render->audioInfo_->bytePerSample());
    auto size = static_cast<uint32_t>(numFrames) * bytesPerFrame;
    auto dataPtr = static_cast<uint8_t*>(audioData);

    AudioDataFlag flag = AudioDataFlag::Normal;
    uint32_t readSize = 0;
    if (auto func = render->dataFunc_.load()) {
        readSize = std::invoke(*func, dataPtr, size, flag);
    }
    if (readSize < size) {
        std::fill_n(dataPtr + readSize, size - readSize, 0);
    }

    const auto vol = render->volume_.load(std::memory_order_relaxed);
    if (vol != 1.0f) {
        auto* samples = reinterpret_cast<int16_t*>(dataPtr);
        const auto sampleCount = static_cast<uint32_t>(numFrames) * render->audioInfo_->channels;
        for (uint32_t i = 0; i < sampleCount; ++i) {
            samples[i] = static_cast<int16_t>(std::clamp(samples[i] * vol,
                                                         static_cast<float>(INT16_MIN),
                                                         static_cast<float>(INT16_MAX)));
        }
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void AAudioRender::errorCallback(AAudioStream* /*stream*/,
                                 void* userData,
                                 aaudio_result_t error) noexcept {
    auto* render = static_cast<AAudioRender*>(userData);
    LogE("AAudio stream error: {}", AAudio_convertResultToText(error));
    const auto prevStatus = render->status_.load();
    std::thread([self = render->shared_from_this(), prevStatus] {
        self->releaseStream();
        self->initStream();
        if (prevStatus == RenderStatus::Playing) {
            self->play();
        }
    }).detach();
}

void AAudioRender::play() noexcept {
    if (!isNormal() || status_ == RenderStatus::Playing) {
        return;
    }
    if (!stream_) {
        return;
    }
    auto result = AAudioStream_requestStart(stream_);
    if (result != AAUDIO_OK) {
        LogE("AAudio requestStart failed: {}", AAudio_convertResultToText(result));
        return;
    }
    aaudio_stream_state_t nextState = AAUDIO_STREAM_STATE_UNINITIALIZED;
    AAudioStream_waitForStateChange(stream_,
                                    AAUDIO_STREAM_STATE_STARTING,
                                    &nextState,
                                    200'000'000LL); // 200ms timeout
    status_ = RenderStatus::Playing;
}

void AAudioRender::pause() noexcept {
    if (status_ == RenderStatus::Pause || status_ == RenderStatus::Stop) {
        return;
    }
    if (!stream_) {
        return;
    }
    auto streamState = AAudioStream_getState(stream_);
    if (streamState == AAUDIO_STREAM_STATE_STARTING) {
        aaudio_stream_state_t nextState = AAUDIO_STREAM_STATE_UNINITIALIZED;
        AAudioStream_waitForStateChange(stream_,
                                        AAUDIO_STREAM_STATE_STARTING,
                                        &nextState,
                                        500'000'000LL);
    }
    AAudioStream_requestPause(stream_);
    aaudio_stream_state_t pausedState = AAUDIO_STREAM_STATE_UNINITIALIZED;
    AAudioStream_waitForStateChange(stream_,
                                    AAUDIO_STREAM_STATE_PAUSING,
                                    &pausedState,
                                    500'000'000LL);
    status_ = RenderStatus::Pause;
}

void AAudioRender::stop() noexcept {
    if (status_ == RenderStatus::Stop) {
        return;
    }
    if (stream_) {
        auto state = AAudioStream_getState(stream_);
        if (state == AAUDIO_STREAM_STATE_PAUSED) {
            AAudioStream_requestStart(stream_);
            aaudio_stream_state_t nextState = AAUDIO_STREAM_STATE_UNINITIALIZED;
            AAudioStream_waitForStateChange(stream_,
                                            AAUDIO_STREAM_STATE_STARTING,
                                            &nextState,
                                            200'000'000LL);
        }
        AAudioStream_requestStop(stream_);
    }
    status_ = RenderStatus::Stop;
}

void AAudioRender::flush() noexcept {
    if (!stream_) {
        return;
    }
    if (status_ == RenderStatus::Playing) {
        AAudioStream_requestPause(stream_);
        aaudio_stream_state_t nextState = AAUDIO_STREAM_STATE_UNINITIALIZED;
        AAudioStream_waitForStateChange(stream_,
                                        AAUDIO_STREAM_STATE_PAUSING,
                                        &nextState,
                                        500'000'000LL);
        status_ = RenderStatus::Pause;
    }
    flushBaseFrames_.store(AAudioStream_getFramesWritten(stream_), std::memory_order_release);
    AAudioStream_requestFlush(stream_);
    aaudio_stream_state_t flushedState = AAUDIO_STREAM_STATE_UNINITIALIZED;
    AAudioStream_waitForStateChange(stream_,
                                    AAUDIO_STREAM_STATE_FLUSHING,
                                    &flushedState,
                                    500'000'000LL);
}

void AAudioRender::reset() noexcept {}

void AAudioRender::renderEnd() noexcept {
    pause();
}

void AAudioRender::setVolume(float volume) noexcept {
    volume_ = volume;
}

Time::TimeDelta AAudioRender::getHardwareLatency() noexcept {
    if (!stream_) {
        return Time::TimeDelta(0);
    }
    const int32_t sampleRate = AAudioStream_getSampleRate(stream_);
    if (sampleRate <= 0) {
        return Time::TimeDelta(0);
    }
    int64_t framePosition = 0;
    int64_t timeNs = 0;
    if (AAudioStream_getTimestamp(stream_, CLOCK_MONOTONIC, &framePosition, &timeNs) == AAUDIO_OK) {
        const int64_t framesWritten = AAudioStream_getFramesWritten(stream_);
        const int64_t pendingFrames = (framesWritten - flushBaseFrames_.load(std::memory_order_acquire)) - framePosition;
        if (pendingFrames > 0) {
            return Time::TimeDelta(pendingFrames * 1'000'000LL / sampleRate);
        }
    }
    const int32_t burst = AAudioStream_getFramesPerBurst(stream_);
    return Time::TimeDelta(static_cast<int64_t>(burst) * 1'000'000LL / sampleRate);
}

} // namespace slark
