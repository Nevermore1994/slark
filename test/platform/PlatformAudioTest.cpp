#include "audio/AudioRenderComponent.h"
#include "AVFrame.hpp"
#include "AudioInfo.h"

#include <gtest/gtest.h>
#include <memory>

using namespace slark;

TEST(PlatformAudioTest, CreateAndDestroy) {
    auto info = std::make_shared<AudioInfo>();
    info->sampleRate = 44100;
    info->channels = 2;
    auto component = std::make_unique<AudioRenderComponent>(info);
    EXPECT_NE(component, nullptr);
}

TEST(PlatformAudioTest, AudioInfoAccess) {
    auto info = std::make_shared<AudioInfo>();
    info->sampleRate = 48000;
    info->channels = 2;
    auto component = std::make_unique<AudioRenderComponent>(info);
    auto retrieved = component->audioInfo();
    EXPECT_EQ(retrieved->sampleRate, 48000);
    EXPECT_EQ(retrieved->channels, 2);
}

TEST(PlatformAudioTest, FlushDoesNotCrash) {
    auto info = std::make_shared<AudioInfo>();
    info->sampleRate = 44100;
    info->channels = 2;
    auto component = std::make_unique<AudioRenderComponent>(info);
    EXPECT_NO_THROW(component->flush());
}

TEST(PlatformAudioTest, SetVolume) {
    auto info = std::make_shared<AudioInfo>();
    info->sampleRate = 44100;
    info->channels = 2;
    auto component = std::make_unique<AudioRenderComponent>(info);
    component->setVolume(0.0f);
    component->setVolume(50.0f);
    component->setVolume(100.0f);
}

TEST(PlatformAudioTest, SeekDoesNotCrash) {
    auto info = std::make_shared<AudioInfo>();
    info->sampleRate = 44100;
    info->channels = 2;
    auto component = std::make_unique<AudioRenderComponent>(info);
    component->seek(0.0);
    component->seek(10.0);
}

TEST(PlatformAudioTest, PlayedTimeIsNonNegative) {
    auto info = std::make_shared<AudioInfo>();
    info->sampleRate = 44100;
    info->channels = 2;
    auto component = std::make_unique<AudioRenderComponent>(info);
    EXPECT_GE(component->playedTime(), Time::TimePoint(0.0));
}

TEST(PlatformAudioTest, SetDecoderCompleted) {
    auto info = std::make_shared<AudioInfo>();
    info->sampleRate = 44100;
    info->channels = 2;
    auto component = std::make_unique<AudioRenderComponent>(info);
    EXPECT_FALSE(component->isDecoderCompleted());
    component->setDecoderCompleted();
    EXPECT_TRUE(component->isDecoderCompleted());
}

TEST(PlatformAudioTest, ResetDoesNotCrash) {
    auto info = std::make_shared<AudioInfo>();
    info->sampleRate = 44100;
    info->channels = 2;
    auto component = std::make_unique<AudioRenderComponent>(info);
    component->reset();
}
