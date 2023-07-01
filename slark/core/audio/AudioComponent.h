//
//  AudioComponent.hpp
//  slark
//
//  Created by Nevermore on 2022/8/17.
//

#pragma once
#include <memory>
#include <functional>
#include "AudioInfo.h"
#include "NonCopyable.h"
#include "AVFrame.hpp"
#include "Time.hpp"
#include "AVFrameDeque.hpp"

namespace slark::Audio {

class AudioRenderComponent: public slark::NonCopyable {
public:
    AudioRenderComponent(AudioInfo info);
    ~AudioRenderComponent() override;

    bool push(AVFramePtr frame);
    AVFramePtr pop();
public:
    std::function<bool(Data)> process;
    std::function<void(AVFramePtr)> completion;
private:
    slark::AVFrameSafeDeque frames_;
};

class AudioRecorderComponent: public slark::NonCopyable {
public:
    ~AudioRecorderComponent() override = default;

    void output(slark::Data data);
public:
    std::function<bool(slark::Data)> process;
private:
    class AudioRecorderComponentImpl;
    std::unique_ptr<AudioRecorderComponentImpl> impl_;
};

}

/* AudioComponent_hpp */
