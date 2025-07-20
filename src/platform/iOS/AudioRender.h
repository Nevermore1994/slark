//
//  AudioRender.h
//  slark
//
//  Created by Nevermore on 2022/8/15.
// Copyright (c) 2023 Nevermore All rights reserved.

#pragma once

#include <AudioToolbox/AudioToolbox.h>
#include "Data.hpp"
#include "AudioInfo.h"
#include "AudioDescription.h"

namespace slark {

class AudioRender : public IAudioRender {
public:
    explicit AudioRender(std::shared_ptr<AudioInfo> audioInfo);
    
    ~AudioRender() override;
    
    void play() noexcept override;
    
    void pause() noexcept override;
    
    void stop() noexcept override;
    
    void reset() noexcept override;
    
    void setVolume(float volume) noexcept override;
    
    void flush() noexcept override;
    
    Time::TimePoint playedTime() noexcept override;
    
    void renderEnd() noexcept override;
    
public:
    bool isNeedRequestData() const noexcept;
    
private:
    Time::TimeDelta getLatency() noexcept;
    
    bool checkFormat() const noexcept;
    
    bool setupAudioComponent() noexcept;
private:
    AudioUnit volumeUnit_ = nullptr;
    AudioUnit renderUnit_ = nullptr;
    Time::TimeDelta latency_;
    TimerId timerId_ = TimerId::kInvalidTimerId;
};

}

