//
// Created by Nevermore on 2025/2/24.
//

#pragma once

#include <string>
#include "Data.hpp"

enum class AudioPlayerAction {
    Play,
    Pause,
    Flush,
    Release,
};

enum class AudioPlayerConfig {
    PlayRate,
    Volume,
};

namespace slark {

struct NativeAudioPlayer {
    static std::string create(uint64_t sampleRate, uint8_t channelCount);

    static void sendAudioData(const std::string& playerId, DataPtr data);

    static void doAction(const std::string& playerId, AudioPlayerAction action);

    static void setConfig(const std::string& playerId, AudioPlayerConfig config, float value);
};



}
