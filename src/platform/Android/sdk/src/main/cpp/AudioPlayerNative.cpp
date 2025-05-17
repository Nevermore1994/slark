//
// Created by Nevermore on 2025/2/24.
//

#include "AudioPlayerNative.h"
#include "Log.hpp"
#include "AudioRender.h"
#include "JNICache.h"
#include "JNISignature.h"
#include "JNIHelper.h"
#include "JNIEnvGuard.hpp"
#include "AudioRender.h"

using namespace slark;
using namespace slark::JNI;

constexpr std::string_view kAudioPlayerClass = "com/slark/sdk/AudioPlayer";
std::string Native_AudioPlayer_createAudioPlayer(JNIEnv *env, jint sampleRate,
                                                 jint channelCount) {
    auto audioPlayerClass = JNICache::shareInstance().getClass(env, kAudioPlayerClass);
    if (!audioPlayerClass) {
        LogE("create player failed, not found class");
        return "";
    }
    auto methodSignature = JNI::makeJNISignature(JNI::String, JNI::Int, JNI::Int);
    auto createMethod= JNICache::shareInstance().getStaticMethodId(audioPlayerClass,
                                                                         "createAudioPlayer",
                                                                         methodSignature);
    if (!createMethod) {
        LogE("create player failed, not found method");
        return "";
    }
    auto jPlayerId = reinterpret_cast<jstring>(env->CallStaticObjectMethod(audioPlayerClass.get(),
                                                          createMethod.get(),
                                                          sampleRate,
                                                          channelCount));
    auto playerId = JNI::FromJVM::toString(env, jPlayerId);
    return playerId;
}

void Native_AudioPlayer_audioPlayerAction(JNIEnv *env, const std::string& playerId,
                                                 AudioPlayerAction action) {
    constexpr std::string_view kActionClass = "com/slark/sdk/AudioPlayer$Action";
    std::string_view fieldView;
    switch (action) {
        case AudioPlayerAction::Play:
            fieldView = "Play";
            break;
        case AudioPlayerAction::Pause:
            fieldView = "Pause";
            break;
        case AudioPlayerAction::Flush:
            fieldView = "Flush";
            break;
        case AudioPlayerAction::Release:
            fieldView = "Release";
            break;
        default:
            std::unreachable();
            return;
    }

    auto enumValue = JNICache::shareInstance().getEnumField(env, kActionClass, fieldView);
    auto audioPlayerClass = JNICache::shareInstance().getClass(env, kAudioPlayerClass);
    if (!audioPlayerClass) {
        LogE("create player failed, not found audio player class");
        return;
    }

    auto jPlayerId = ToJVM::toString(env, playerId);
    auto signature = JNI::makeJNISignature(JNI::Void, JNI::String, JNI::makeObject(kActionClass));
    auto actionMethodId = JNICache::shareInstance().getStaticMethodId(audioPlayerClass, "audioPlayerAction", signature);
    env->CallStaticVoidMethod(audioPlayerClass.get(),
                              actionMethodId.get(),
                              jPlayerId.get(),
                              enumValue.get());
}

void Native_AudioPlayer_setAudioConfig(JNIEnv *env, const std::string& playerId,
                                       AudioPlayerConfig config, float value) {
    constexpr std::string_view kConfigClass = "com/slark/sdk/AudioPlayer$Config";
    std::string_view fieldView;
    if (config == AudioPlayerConfig::PlayRate) {
        fieldView = "Rate";
    } else if (config == AudioPlayerConfig::Volume) {
        fieldView = "Volume";
    }

    auto enumValue = JNICache::shareInstance().getEnumField(env, kConfigClass, fieldView);
    auto audioPlayerClass = JNICache::shareInstance().getClass(env, kAudioPlayerClass);
    if (!audioPlayerClass) {
        LogE("create player failed, not found audio player class");
        return;
    }

    auto jPlayerId = ToJVM::toString(env, playerId);
    auto signature = JNI::makeJNISignature(JNI::Void, JNI::String, JNI::makeObject(kConfigClass));
    auto methodId = JNICache::shareInstance().getStaticMethodId(audioPlayerClass,
                                                                     "audioPlayerConfig",
                                                                     signature);
    if (!methodId) {
        LogE("not found method!");
        return;
    }
    env->CallStaticVoidMethod(audioPlayerClass.get(),
                              methodId.get(),
                              jPlayerId.get(),
                              enumValue.get(),
                              static_cast<jfloat>(value));
}

uint64_t Native_AudioPlayer_getPlayedTime(JNIEnv *env, const std::string& playerId) {
    auto audioPlayerClass = JNICache::shareInstance().getClass(env, kAudioPlayerClass);
    if (!audioPlayerClass) {
        LogE("not found audio player class");
        return 0;
    }
    auto signature = JNI::makeJNISignature(JNI::Long, JNI::String);
    auto method = JNICache::shareInstance().getStaticMethodId(audioPlayerClass, "", signature);
    if (!method) {
        LogE("not found audio player method");
        return 0;
    }
    auto jPlayerId = ToJVM::toString(env, playerId);
    auto playedTime = env->CallStaticLongMethod(audioPlayerClass.get(), method.get(), jPlayerId.get());
    return static_cast<uint64_t>(playedTime);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_slark_sdk_AudioPlayer_requestAudioData(JNIEnv *env, jobject /*thiz*/, jstring jPlayerId, jobject buffer) {
    constexpr std::string_view kDataFlagClass = "com/slark/sdk/DataFlag";
    void* nativePtr = env->GetDirectBufferAddress(buffer);
    auto requestSize = static_cast<uint32_t>(env->GetDirectBufferCapacity(buffer));
    AudioDataFlag flag = AudioDataFlag::Error;
    do {
        if (nativePtr == nullptr || requestSize <= 0) {
            break;
        }

        auto playerId = FromJVM::toString(env, jPlayerId);
        if (auto player = AudioRenderManager::shareInstance().find(playerId);
            player) {
            auto size= player->requestAudioData(reinterpret_cast<uint8_t*>(nativePtr), requestSize, flag);
            LogI("request audio data: {}, already applied: {}", requestSize, size);
            if (size != requestSize) {
                if (flag == AudioDataFlag::EndOfStream) {
                    LogI("render audio data end of stream");
                } else {
                    LogE("missing data may result in noise");
                }
            }
        } else {
            LogE("not found player");
            break;
        }
    } while(false);
    std::string_view fieldView;
    switch (flag) {
        case AudioDataFlag::Error : { fieldView = "Error"; break; }
        case AudioDataFlag::Normal : { fieldView = "Normal"; break; }
        case AudioDataFlag::EndOfStream : { fieldView = "EndOfStream"; break; }
        case AudioDataFlag::Silence : { fieldView = "Silence"; break; }
    }
    auto field = JNICache::shareInstance().getEnumField(env, kDataFlagClass, fieldView);
    return field.get();
}

namespace slark {

using namespace slark::JNI;

std::string NativeAudioPlayer::create(uint64_t sampleRate, uint8_t channelCount) {
    JNIEnvGuard envGuard(getJavaVM());
    auto playerId= Native_AudioPlayer_createAudioPlayer(envGuard.get(),
                                                        static_cast<jint>(sampleRate),
                                                        channelCount);
    return playerId;
}

uint64_t NativeAudioPlayer::getPlayedTime(const std::string& playerId) {
    JNIEnvGuard envGuard(getJavaVM());
    return Native_AudioPlayer_getPlayedTime(envGuard.get(), playerId);
}


void NativeAudioPlayer::doAction(const std::string &playerId, AudioPlayerAction action) {
    JNIEnvGuard envGuard(getJavaVM());
    Native_AudioPlayer_audioPlayerAction(envGuard.get(), playerId, action);
}

void NativeAudioPlayer::setConfig(const std::string &playerId, AudioPlayerConfig config, float value) {
    JNIEnvGuard envGuard(getJavaVM());
    Native_AudioPlayer_setAudioConfig(envGuard.get(), playerId, config, value);
}

}