//
// Created by Nevermore on 2025/2/24.
//

#include "AudioPlayerNative.h"
#include "Log.hpp"
#include "SlarkNative.h"
#include "AudioRender.h"

using namespace slark;

std::string Native_AudioPlayer_createAudioPlayer(JNIEnv *env, jint sampleRate,
                                                 jint channelCount) {
    auto audioPlayerClass = env->FindClass("com/slark/sdk/AudioPlayer");
    if (audioPlayerClass == nullptr) {
        LogE("create player failed, not found audio player class");
        return "";
    }
    jmethodID createAudioPlayer = env->GetStaticMethodID(audioPlayerClass, "createAudioPlayer", "(II)Ljava/lang/String;");
    if (createAudioPlayer == nullptr) {
        LogE("create player failed, not found audio player method");
        return "";
    }
    auto jPlayerId = (jstring)env->CallStaticObjectMethod(audioPlayerClass, createAudioPlayer, sampleRate, channelCount);
    auto cPlayerId = env->GetStringUTFChars(jPlayerId, nullptr);
    auto playerId = std::string(cPlayerId);
    env->ReleaseStringUTFChars(jPlayerId, cPlayerId);
    return playerId;
}

void Native_AudioPlayer_sendAudioData(JNIEnv *env, const std::string& playerId,
                                                 DataPtr data) {
    auto audioPlayerClass = env->FindClass("com/slark/sdk/AudioPlayer");
    if (audioPlayerClass == nullptr) {
        LogE("create player failed, not found audio player class");
        return;
    }
    jmethodID createAudioPlayer = env->GetStaticMethodID(audioPlayerClass, "sendAudioData", "(Ljava/lang/String;[B)V");
    if (createAudioPlayer == nullptr) {
        LogE("create player failed, not found audio player method");
        return;
    }
    jstring jPlayerId = env->NewStringUTF(playerId.c_str());
    auto size = static_cast<jsize>(data->length);
    jbyteArray dataArray = env->NewByteArray(size);
    if (dataArray == nullptr) {
        LogE("create data array error");
        return;
    }
    env->SetByteArrayRegion(dataArray, 0, size, reinterpret_cast<jbyte*>(data->rawData));
    env->CallStaticVoidMethod(audioPlayerClass, createAudioPlayer, jPlayerId, dataArray);
}

void Native_AudioPlayer_audioPlayerAction(JNIEnv *env, const std::string& playerId,
                                                 AudioPlayerAction action) {
    jclass actionClass = env->FindClass("com/slark/sdk/AudioPlayer$Action");
    if (actionClass == nullptr) {
        return;
    }

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
    jfieldID fieldId = env->GetStaticFieldID(actionClass, fieldView.data(), "Lcom/slark/sdk/AudioPlayer$Action;");
    jobject enumValue = env->GetStaticObjectField(actionClass, fieldId);
    auto audioPlayerClass = env->FindClass("com/slark/sdk/AudioPlayer");
    if (audioPlayerClass == nullptr) {
        LogE("create player failed, not found audio player class");
        return;
    }

    jstring jPlayerId = env->NewStringUTF(playerId.c_str());
    jmethodID actionMethodId = env->GetStaticMethodID(audioPlayerClass, "audioPlayerAction", "(Ljava/lang/String;Lcom/slark/sdk/AudioPlayer$Action;)V");
    env->CallStaticVoidMethod(audioPlayerClass, actionMethodId, jPlayerId, enumValue);
}

void Native_AudioPlayer_setAudioConfig(JNIEnv *env, const std::string& playerId,
                                              AudioPlayerConfig config, float value) {
    jclass configClass = env->FindClass("com/slark/sdk/AudioPlayer$Config");
    if (configClass == nullptr) {
        return;
    }

    std::string_view fieldView;
    if (config == AudioPlayerConfig::PlayRate) {
        fieldView = "PLAY_RATE";
    } else if (config == AudioPlayerConfig::Volume) {
        fieldView = "PLAY_VOL";
    }

    jfieldID fieldId = env->GetStaticFieldID(configClass, fieldView.data(), "Lcom/slark/sdk/AudioPlayer$Config;");
    jobject enumValue = env->GetStaticObjectField(configClass, fieldId);
    auto audioPlayerClass = env->FindClass("com/slark/sdk/AudioPlayer");
    if (audioPlayerClass == nullptr) {
        LogE("create player failed, not found audio player class");
        return;
    }

    jstring jPlayerId = env->NewStringUTF(playerId.c_str());
    jmethodID methodId = env->GetStaticMethodID(audioPlayerClass, "audioPlayerConfig", "(Ljava/lang/String;Lcom/slark/sdk/AudioPlayer$Config;F)V");
    env->CallStaticVoidMethod(audioPlayerClass, methodId, jPlayerId, enumValue, static_cast<jfloat>(value));
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
