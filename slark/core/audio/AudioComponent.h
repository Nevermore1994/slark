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
#include "AVFrameSafeDeque.hpp"
#include "Node.h"

namespace slark::Audio {

class AudioRenderComponent: public slark::NonCopyable, public InputNode {
public:
    explicit AudioRenderComponent(AudioInfo info);
    ~AudioRenderComponent() override = default;

    void receive(AVFrameRefPtr frame) noexcept override;
    void process(AVFrameRefPtr frame) noexcept override;
public:
    std::function<void(AVFrameRefPtr)> completion;
private:
    class AudioRenderComponentImpl;
    std::unique_ptr<AudioRenderComponentImpl> pimpl_;
};

class AudioRecorderComponent: public slark::NonCopyable, public OutputNode {
public:
    AudioRecorderComponent() = default;
    ~AudioRecorderComponent() override = default;

    void process(AVFrameRefPtr frame) noexcept override;
    void send(AVFrameRefPtr frame) noexcept override;
private:
    class AudioRecorderComponentImpl;
    std::unique_ptr<AudioRecorderComponentImpl> pimpl_;
};

}

/* AudioComponent_hpp */
