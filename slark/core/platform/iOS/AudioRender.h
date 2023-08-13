//
//  AudioRender.h
//  slark
//
//  Created by Nevermore on 2022/8/15.
//

#pragma once

#import <AudioToolbox/AudioToolbox.h>
#include <memory>
#include "Data.hpp"
#include "AudioDefine.h"

namespace slark::Audio {

using AudioForamt = AudioStreamBasicDescription;

class AudioRender {
public:
    AudioRender(std::unique_ptr<AudioForamt> format);
    ~AudioRender();
    
    void play() noexcept;
    void pause() noexcept;
    void stop() noexcept;
    void setVolume(float volume) noexcept;
    void flush() noexcept;
    
    inline AudioRenderStatus status() const noexcept {
        return status_;
    }
    
    AudioStreamBasicDescription format() const noexcept {
        return *format_;
    }
    
    bool isNeedRequestData() const noexcept;
    
    std::function<std::unique_ptr<slark::Data>(uint32_t)> requestNextAudioData;
private:
    bool checkFormat() const noexcept;
    bool setupAUGraph() noexcept;
private:
    AUGraph auGraph_;
    AUNode renderIndex_;
    AudioUnit renderUnit_;
    AUNode volumeIndex_;
    AudioUnit volumeUnit_;
    std::unique_ptr<AudioForamt> format_;
    AudioRenderStatus status_ = AudioRenderStatus::Unknown;
};

}
