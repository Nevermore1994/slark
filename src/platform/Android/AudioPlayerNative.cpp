//
// Created by Nevermore on 2025/2/24.
//

#include "AudioPlayerNative.h"
#include "Log.hpp"
#include "SlarkNative.h"
#include "AudioRender.h"
#include "JNICache.h"
#include "JNISignature.h"
#include "JNIHelper.h"
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
    auto jPlayerId = (jstring)env->CallStaticObjectMethod(audioPlayerClass.get(),
                                                          createMethod.get(),
                                                          sampleRate,
                                                          channelCount);
    auto playerId = JNI::FromJVM::toString(env, jPlayerId);
    return playerId;
}

void Native_AudioPlayer_sendAudioData(JNIEnv *env,
                                      const std::string& playerId,
                                      DataPtr data) {
    auto audioPlayerClass = JNICache::shareInstance().getClass(env, kAudioPlayerClass);
    if (!audioPlayerClass) {
        LogE("create player failed, not found audio player class");
        return;
    }
    auto signature = JNI::makeJNISignature(JNI::Void, JNI::String, JNI::makeArray(JNI::Byte));
    auto method = JNICache::shareInstance().getStaticMethodId(audioPlayerClass, "sendAudioData", signature);
    if (!method) {
        LogE("create player failed, not found audio player method");
        return;
    }

    auto jPlayerId = ToJVM::toString(env, playerId);
    auto dataArray = ToJVM::toByteArray(env, *data);
    env->CallStaticVoidMethod(audioPlayerClass.get(), method.get(), jPlayerId.get(), dataArray.get());
}

void Native_AudioPlayer_audioPlayerAction(JNIEnv *env, const std::string& playerId,
                                                 AudioPlayerAction action) {
    constexpr std::string_view kActionClass = "com/slark/sdk/AudioPlayer$Action";
    std::string_view fieldView;
    switch (action) {
        case AudioPlayerAction::Play:
            fieldView = "PLAY";
            break;
        case AudioPlayerAction::Pause:
            fieldView = "PAUSE";
            break;
        case AudioPlayerAction::Flush:
            fieldView = "FLUSH";
            break;
        case AudioPlayerAction::Release:
            fieldView = "RELEASE";
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
        fieldView = "PLAY_RATE";
    } else if (config == AudioPlayerConfig::Volume) {
        fieldView = "PLAY_VOL";
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

extern "C"
JNIEXPORT jobject JNICALL
Java_com_slark_sdk_AudioPlayer_requestAudioData(JNIEnv *env, jobject thiz, jstring jPlayerId, jint requestSize) {
    constexpr std::string_view kAudioDataResultClass = "com/slark/sdk/AudioDataResult";
    constexpr std::string_view kDataFlagClass = "com/slark/sdk/DataFlag";
    auto playerId = FromJVM::toString(env, jPlayerId);
    Data data(requestSize);
    if (auto player = AudioRenderManager::shareInstance().find(playerId);
        player && player->requestAudioData) {
        AudioDataFlag flag = AudioDataFlag::Normal;
        auto size = player->requestAudioData(data.rawData, requestSize, flag);
        data.length = size;
        auto resultClass= JNICache::shareInstance().getClass(env, kAudioDataResultClass);
        auto signature = JNI::makeJNISignature(JNI::Void, JNI::makeArray(JNI::Byte), JNI::makeObject(kDataFlagClass));
        auto constructorMethodId = JNICache::shareInstance().getMethodId(resultClass, "<init>", signature);
        if (!constructorMethodId) {
            LogE("not found method!");
        }
        auto dataArray = ToJVM::toByteArray(env, data);
        jobject result = env->NewObject(resultClass.get(), constructorMethodId.get(), dataArray.get(), flag);
        return result;
    } else {
        LogE("not found player");
    }
    return nullptr;
}

namespace slark {

std::string NativeAudioPlayer::create(uint64_t sampleRate, uint8_t channelCount) {
    JNIEnvGuard envGuard(getJavaVM());
    auto playerId= Native_AudioPlayer_createAudioPlayer(envGuard.get(),
                                                        static_cast<jint>(sampleRate),
                                                        channelCount);
    return playerId;
}

void NativeAudioPlayer::sendAudioData(const std::string &playerId, DataPtr data) {
    JNIEnvGuard envGuard(getJavaVM());
    Native_AudioPlayer_sendAudioData(envGuard.get(), playerId, std::move(data));
}

void NativeAudioPlayer::doAction(const std::string &playerId, AudioPlayerAction action) {
    JNIEnvGuard envGuard(getJavaVM());
    Native_AudioPlayer_audioPlayerAction(envGuard.get(), playerId, action);
}

void
NativeAudioPlayer::setConfig(const std::string &playerId, AudioPlayerConfig config, float value) {
    JNIEnvGuard envGuard(getJavaVM());
    Native_AudioPlayer_setAudioConfig(envGuard.get(), playerId, config, value);
}

}