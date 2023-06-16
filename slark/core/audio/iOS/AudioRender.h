//
//  AudioRender.h
//  slark
//
//  Created by Nevermore on 2022/8/15.
//

#pragma once

#import <AudioToolbox/AudioToolbox.h>
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
    
    inline AudioRenderStatus status() const noexcept {
        return status_;
    }
    
    AudioStreamBasicDescription format() const noexcept {
        return *format_;
    }
    
    std::function<slark::Data(uint32_t, double)> requestNextAudioData;
private:
    bool setupAudioUnit() noexcept;
private:
    AudioComponentInstance audioUnit_;
    std::unique_ptr<AudioForamt> format_;
    AudioRenderStatus status_;
};

}
