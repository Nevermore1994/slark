//
//  AudioRecorderComponent.hpp
//  slark
//
// Copyright (c) 2023 Nevermore All rights reserved.
#pragma once
#include <memory>
#include <functional>
#include "AudioDefine.h"
#include "NonCopyable.h"
#include "AVFrame.hpp"
#include "Time.hpp"
#include "Node.h"

namespace slark::Audio {

class AudioRecorderComponent: public slark::NonCopyable, public OutputNode {
public:
    AudioRecorderComponent() = default;
    ~AudioRecorderComponent() override = default;

    void process(AVFrameRefPtr frame) noexcept override;
    void send(AVFrameRefPtr frame) noexcept override;
private:
};

}

