//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "AccessGate.hpp"
#include "AudioInfo.h"
#include "AudioSink.h"
#include "SPSCRingBuffer.hpp"

namespace slark::engine {

class AudioRenderSink final : public AudioSink {
 public:
    using RenderFactory = std::function<std::shared_ptr<slark::IAudioRender>(
        const std::shared_ptr<slark::AudioInfo>&)>;

    explicit AudioRenderSink(RenderFactory factory, double prefillSec = 0.15);
    ~AudioRenderSink() override;

    bool open(std::shared_ptr<slark::AudioInfo> info) override;
    bool write(AVFrameRefPtr frame) override;
    [[nodiscard]] Time::TimePoint position() const override;
    [[nodiscard]] bool primed() const override;
    void start() override;
    void pause() override;
    void setVolume(float volume) override;
    void flush(Time::TimePoint offset) override;
    void markDecoderEnded() override;
    [[nodiscard]] bool drained() const override;

 private:
    static constexpr uint32_t kBufferBytes = 262144;

    struct DataPlane {
        slark::SPSCRingBuffer<uint8_t, kBufferBytes> buffer;
        slark::AccessGate gate;
        std::atomic<uint64_t> consumedBytes{0};
        std::atomic<uint64_t> offsetUs{0};
        std::atomic<uint64_t> lastPositionUs{0};
        std::atomic<bool> decoderEnded{false};
    };

    static uint32_t provide(DataPlane& plane,
                            uint8_t* data,
                            uint32_t size,
                            slark::AudioDataFlag& flag) noexcept;

    RenderFactory factory_;
    double prefillSec_;
    std::shared_ptr<slark::AudioInfo> info_;
    std::shared_ptr<slark::IAudioRender> render_;
    std::shared_ptr<DataPlane> plane_;
};

// Platform libraries register during initialization; until then no audio pipeline can be built.
void setPlatformAudioRenderFactory(AudioRenderSink::RenderFactory factory) noexcept;
[[nodiscard]] AudioRenderSink::RenderFactory platformAudioRenderFactory() noexcept;

}  // namespace slark::engine
