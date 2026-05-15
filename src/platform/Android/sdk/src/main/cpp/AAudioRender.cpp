// Copyright (c) 2026 Nevermore-. All rights reserved.

#include "AAudioRender.h"

#include "Log.hpp"

#include <algorithm>

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
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
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
    status_ = RenderStatus::Playing;
}

void AAudioRender::pause() noexcept {
    if (status_ == RenderStatus::Pause || status_ == RenderStatus::Stop) {
        return;
    }
    if (!stream_) {
        return;
    }
    AAudioStream_requestPause(stream_);
    status_ = RenderStatus::Pause;
}

void AAudioRender::stop() noexcept {
    if (status_ == RenderStatus::Stop) {
        return;
    }
    if (stream_) {
        AAudioStream_requestStop(stream_);
    }
    status_ = RenderStatus::Stop;
}

void AAudioRender::flush() noexcept {
    if (!stream_) {
        return;
    }
    // AAudio requestFlush 只能在 Paused 状态调用
    if (status_ == RenderStatus::Playing) {
        AAudioStream_requestPause(stream_);
    }
    AAudioStream_requestFlush(stream_);
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
    auto burst = AAudioStream_getFramesPerBurst(stream_);
    auto sampleRate = AAudioStream_getSampleRate(stream_);
    if (sampleRate <= 0) {
        return Time::TimeDelta(0);
    }
    auto latencyUs = static_cast<int64_t>(burst) * 1'000'000LL / sampleRate;
    return Time::TimeDelta(latencyUs);
}

} // namespace slark
