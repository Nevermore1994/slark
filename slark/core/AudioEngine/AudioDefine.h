//
//  AudioDefine.h
//  slark
//
//  Created by Nevermore on 2022/8/19.
//
#pragma once
#include "AVFrame.hpp"

namespace slark::Audio {

enum class AudioRenderStatus {
    Unknow,
    Play,
    Pause,
    Stop,
};

}
