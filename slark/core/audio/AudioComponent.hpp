//
//  AudioComponent.hpp
//  slark
//
//  Created by Nevermore on 2022/8/17.
//

#pragma once
#include <memory>
#include <functional>
#include "AudioInfo.hpp"
#include "NonCopyable.hpp"
#include "AVFrame.hpp"
#include "Time.hpp"
#include "AVFrameDeque.hpp"

namespace Slark::Audio {

class AudioRenderComponent: public Slark::NonCopyable {
public:
    AudioRenderComponent(AudioInfo info);
    ~AudioRenderComponent() override;

    bool push(AVFramePtr frame);
    AVFramePtr pop();
public:
    std::function<bool(Slark::Data)> process;
    std::function<void(AVFramePtr)> completion;
private:
    Slark::AVFrameSafeDeque frames_;
};

class AudioRecorderComponent: public Slark::NonCopyable {
public:
    ~AudioRecorderComponent() override = default;

    void output(Slark::Data data);
public:
    std::function<bool(Slark::Data)> process;
private:
    class AudioRecorderComponentImpl;
    std::unique_ptr<AudioRecorderComponentImpl> impl_;
};

}

/* AudioComponent_hpp */
