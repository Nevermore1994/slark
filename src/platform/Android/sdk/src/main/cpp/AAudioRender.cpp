//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "AAudioRender.h"

#include <algorithm>

#include "AccessGate.hpp"
#include "Log.hpp"
#include "ScopeGuard.h"

namespace slark {

AAudioRender::AAudioRender(std::shared_ptr<AudioInfo> audioInfo)
    : IAudioRender(std::move(audioInfo)) {
    initStream();
}

AAudioRender::~AAudioRender() {
    closing_.store(true, std::memory_order_release);
    releaseStream();
}

void AAudioRender::initStream() noexcept {
    std::lock_guard lock(streamMutex_);
    if (closing_.load(std::memory_order_acquire)) {
        return;
    }

    ScopeGuard cleanup([this] {
        if (stream_) {
            AAudioStream_close(stream_);
            stream_ = nullptr;
        }
        if (builder_) {
            AAudioStreamBuilder_delete(builder_);
            builder_ = nullptr;
        }
    });

    auto result = AAudio_createStreamBuilder(&builder_);
    if (result != AAUDIO_OK || builder_ == nullptr) {
        LogE("AAudio createStreamBuilder failed: {}", AAudio_convertResultToText(result));
        status_ = RenderStatus::Error;
        return;
    }
    AAudioStreamBuilder_setDataCallback(builder_, dataCallback, this);
    AAudioStreamBuilder_setErrorCallback(builder_, errorCallback, this);
    AAudioStreamBuilder_setDirection(builder_, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSampleRate(builder_, static_cast<int32_t>(audioInfo_->sampleRate));
    AAudioStreamBuilder_setChannelCount(builder_, static_cast<int32_t>(audioInfo_->channels));
    AAudioStreamBuilder_setFormat(builder_, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setPerformanceMode(builder_, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);

    result = AAudioStreamBuilder_openStream(builder_, &stream_);
    if (result != AAUDIO_OK) {
        LogE("AAudio openStream failed: {}", AAudio_convertResultToText(result));
        status_ = RenderStatus::Error;
        return;
    }
    if (closing_.load(std::memory_order_acquire)) {
        return;
    }
    cleanup.dismiss();
    flushBaseFrames_.store(0, std::memory_order_release);
    callbackGate_.open();
    status_ = RenderStatus::Ready;
}

void AAudioRender::releaseStream() noexcept {
    callbackGate_.closeAndWait();
    std::lock_guard lock(streamMutex_);
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
    AccessGate::Scope callback(render ? &render->callbackGate_ : nullptr);
    if (!callback || numFrames <= 0 || audioData == nullptr) {
        return AAUDIO_CALLBACK_RESULT_STOP;
    }

    auto bytesPerFrame = static_cast<uint32_t>(render->audioInfo_->bytePerSample());
    auto size = static_cast<uint32_t>(numFrames) * bytesPerFrame;
    auto dataPtr = static_cast<uint8_t*>(audioData);

    AudioDataFlag flag = AudioDataFlag::Normal;
    const auto readSize = render->requestAudioData(dataPtr, size, flag);
    if (readSize < size) {
        std::fill_n(dataPtr + readSize, size - readSize, 0);
    }

    const auto vol = render->volume_.load(std::memory_order_relaxed);
    if (vol != 1.0f) {
        auto* samples = reinterpret_cast<int16_t*>(dataPtr);
        const auto sampleCount = static_cast<uint32_t>(numFrames) * render->audioInfo_->channels;
        for (uint32_t i = 0; i < sampleCount; ++i) {
            samples[i] = static_cast<int16_t>(std::clamp(
                samples[i] * vol, static_cast<float>(INT16_MIN), static_cast<float>(INT16_MAX)));
        }
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void AAudioRender::errorCallback(AAudioStream* /*stream*/,
                                 void* userData,
                                 aaudio_result_t error) noexcept {
    auto* render = static_cast<AAudioRender*>(userData);
    AccessGate::Scope callback(render ? &render->callbackGate_ : nullptr);
    if (!callback) {
        return;
    }

    render->recoveryError_.store(error, std::memory_order_release);
    render->recoveryRequested_.store(true, std::memory_order_release);
}

void AAudioRender::recoverIfNeeded() noexcept {
    if (!recoveryRequested_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    const auto error = recoveryError_.exchange(AAUDIO_OK, std::memory_order_acq_rel);
    LogE("AAudio stream error: {}", AAudio_convertResultToText(error));
    const auto previousStatus = status_.load(std::memory_order_acquire);
    releaseStream();
    if (closing_.load(std::memory_order_acquire)) {
        return;
    }
    initStream();
    if (previousStatus == RenderStatus::Playing) {
        play();
    }
}

void AAudioRender::play() noexcept {
    recoverIfNeeded();
    std::lock_guard lock(streamMutex_);
    if (closing_.load(std::memory_order_acquire) || !isNormal() ||
        status_ == RenderStatus::Playing || !stream_) {
        return;
    }
    callbackGate_.open();
    auto result = AAudioStream_requestStart(stream_);
    if (result != AAUDIO_OK) {
        LogE("AAudio requestStart failed: {}", AAudio_convertResultToText(result));
        callbackGate_.closeAndWait();
        return;
    }
    aaudio_stream_state_t nextState = AAUDIO_STREAM_STATE_UNINITIALIZED;
    AAudioStream_waitForStateChange(stream_, AAUDIO_STREAM_STATE_STARTING, &nextState,
                                    200'000'000LL);
    status_ = RenderStatus::Playing;
}

void AAudioRender::pause() noexcept {
    callbackGate_.closeAndWait();
    std::lock_guard lock(streamMutex_);
    if (status_ == RenderStatus::Pause || status_ == RenderStatus::Stop || !stream_) {
        return;
    }
    auto streamState = AAudioStream_getState(stream_);
    if (streamState == AAUDIO_STREAM_STATE_STARTING) {
        aaudio_stream_state_t nextState = AAUDIO_STREAM_STATE_UNINITIALIZED;
        AAudioStream_waitForStateChange(stream_, AAUDIO_STREAM_STATE_STARTING, &nextState,
                                        500'000'000LL);
    }
    AAudioStream_requestPause(stream_);
    aaudio_stream_state_t pausedState = AAUDIO_STREAM_STATE_UNINITIALIZED;
    AAudioStream_waitForStateChange(stream_, AAUDIO_STREAM_STATE_PAUSING, &pausedState,
                                    500'000'000LL);
    status_ = RenderStatus::Pause;
}

void AAudioRender::stop() noexcept {
    callbackGate_.closeAndWait();
    std::lock_guard lock(streamMutex_);
    if (status_ == RenderStatus::Stop) {
        return;
    }
    if (stream_) {
        auto state = AAudioStream_getState(stream_);
        if (state == AAUDIO_STREAM_STATE_PAUSED) {
            AAudioStream_requestStart(stream_);
            aaudio_stream_state_t nextState = AAUDIO_STREAM_STATE_UNINITIALIZED;
            AAudioStream_waitForStateChange(stream_, AAUDIO_STREAM_STATE_STARTING, &nextState,
                                            200'000'000LL);
        }
        AAudioStream_requestStop(stream_);
        aaudio_stream_state_t stoppedState = AAUDIO_STREAM_STATE_UNINITIALIZED;
        AAudioStream_waitForStateChange(stream_, AAUDIO_STREAM_STATE_STOPPING, &stoppedState,
                                        500'000'000LL);
    }
    status_ = RenderStatus::Stop;
}

void AAudioRender::flush() noexcept {
    callbackGate_.closeAndWait();
    std::lock_guard lock(streamMutex_);
    if (!stream_) {
        return;
    }
    if (status_ == RenderStatus::Playing) {
        AAudioStream_requestPause(stream_);
        aaudio_stream_state_t nextState = AAUDIO_STREAM_STATE_UNINITIALIZED;
        AAudioStream_waitForStateChange(stream_, AAUDIO_STREAM_STATE_PAUSING, &nextState,
                                        500'000'000LL);
        status_ = RenderStatus::Pause;
    }
    flushBaseFrames_.store(AAudioStream_getFramesWritten(stream_), std::memory_order_release);
    AAudioStream_requestFlush(stream_);
    aaudio_stream_state_t flushedState = AAUDIO_STREAM_STATE_UNINITIALIZED;
    AAudioStream_waitForStateChange(stream_, AAUDIO_STREAM_STATE_FLUSHING, &flushedState,
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
    recoverIfNeeded();
    std::lock_guard lock(streamMutex_);
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
        const int64_t pendingFrames =
            (framesWritten - flushBaseFrames_.load(std::memory_order_acquire)) - framePosition;
        if (pendingFrames > 0) {
            return Time::TimeDelta(pendingFrames * 1'000'000LL / sampleRate);
        }
    }
    const int32_t burst = AAudioStream_getFramesPerBurst(stream_);
    return Time::TimeDelta(static_cast<int64_t>(burst) * 1'000'000LL / sampleRate);
}

}  // namespace slark
