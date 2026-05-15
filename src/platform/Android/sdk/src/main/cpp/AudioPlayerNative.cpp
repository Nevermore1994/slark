//
// Created by Nevermore on 2025/2/24.
//

#include "AudioPlayerNative.h"

#include "AudioRender.h"
#include "JNICache.h"
#include "JNICall.h"
#include "JNIEnvGuard.h"
#include "JNIMarshal.h"
#include "JNISignature.h"
#include "Log.hpp"

using namespace slark;
using namespace slark::JNI;

constexpr std::string_view kAudioPlayerClass = "com/slark/sdk/AudioPlayer";

std::string Native_AudioPlayer_createAudioPlayer(JNIEnv* env, jint sampleRate, jint channelCount) {
    auto playerId = JNI::JNICall::callStatic<std::string>(env,
                                                          kAudioPlayerClass,
                                                          "createAudioPlayer",
                                                          static_cast<int32_t>(sampleRate),
                                                          static_cast<int32_t>(channelCount));
    if (playerId.empty()) {
        LogE("create player failed");
    }
    return playerId;
}

void Native_AudioPlayer_audioPlayerAction(JNIEnv* env,
                                          const std::string& playerId,
                                          AudioPlayerAction action) {
    constexpr std::string_view kActionClass = "com/slark/sdk/AudioPlayer$Action";
    static std::vector<std::string_view> kAudioPlayerAction = {
        "Play",
        "Pause",
        "Flush",
        "Release",
    };
    auto tAction = static_cast<size_t>(action);
    if (tAction >= kAudioPlayerAction.size()) {
        LogE("action is invalid.");
        return;
    }
    std::string_view fieldView = kAudioPlayerAction[static_cast<size_t>(action)];

    auto enumValue = JNICache::instance().getEnumField(env, kActionClass, fieldView);
    if (!enumValue) {
        LogE("not found enum field: {}", fieldView);
        return;
    }
    JNI::JNICall::callStatic<void>(
        env,
        kAudioPlayerClass,
        "audioPlayerAction",
        JNI::JObjectWithSig(ToJVM::str(env, playerId), JNI::Sig::String),
        JNI::JObjectWithSig(std::move(enumValue), JNI::makeObject(kActionClass)));
}

void Native_AudioPlayer_setAudioConfig(JNIEnv* env,
                                       const std::string& playerId,
                                       AudioPlayerConfig config,
                                       float value) {
    constexpr std::string_view kConfigClass = "com/slark/sdk/AudioPlayer$Config";
    std::string_view fieldView;
    if (config == AudioPlayerConfig::PlayRate) {
        fieldView = "Rate";
    } else if (config == AudioPlayerConfig::Volume) {
        fieldView = "Volume";
    }

    auto enumValue = JNICache::instance().getEnumField(env, kConfigClass, fieldView);
    if (!enumValue) {
        LogE("not found enum class:{}", fieldView);
        return;
    }
    JNI::JNICall::callStatic<void>(
        env,
        kAudioPlayerClass,
        "audioPlayerConfig",
        JNI::JObjectWithSig(ToJVM::str(env, playerId), JNI::Sig::String),
        JNI::JObjectWithSig(std::move(enumValue), JNI::makeObject(kConfigClass)),
        value);
}

uint64_t Native_AudioPlayer_getPlayedTime(JNIEnv* env, const std::string& playerId) {
    auto playedTime =
        JNI::JNICall::callStatic<int64_t>(env, kAudioPlayerClass, "getPlayedTime", playerId);
    return static_cast<uint64_t>(playedTime.value_or(0));
}

// 返回值编码规则：高 8 位存 AudioDataFlag，低 24 位存实际填充字节数
// Java 侧解码：flag = (result >>> 24) & 0xFF，size = result & 0x00FFFFFF
static inline jint encodeAudioDataResult(AudioDataFlag flag, uint32_t size) noexcept {
    return static_cast<jint>((static_cast<uint32_t>(flag) << 24) | (size & 0x00FFFFFFu));
}

extern "C" JNIEXPORT jint JNICALL Java_com_slark_sdk_AudioPlayer_requestAudioData(JNIEnv* env,
                                                                                  jobject /*thiz*/,
                                                                                  jstring jPlayerId,
                                                                                  jobject buffer) {
    void* nativePtr = env->GetDirectBufferAddress(buffer);
    auto requestSize = static_cast<uint32_t>(env->GetDirectBufferCapacity(buffer));
    uint32_t requestedSize = 0;
    AudioDataFlag flag = AudioDataFlag::Normal;
    do {
        if (nativePtr == nullptr || requestSize <= 0) {
            break;
        }

        auto playerId = FromJVM::str(env, jPlayerId);
        if (auto player = AudioRenderManager::shareInstance().find(playerId); player) {
            requestedSize =
                player->requestAudioData(reinterpret_cast<uint8_t*>(nativePtr), requestSize, flag);
            LogI("request audio data: {}, already applied: {}", requestSize, requestedSize);
            if (requestedSize != requestSize) {
                if (flag == AudioDataFlag::EndOfStream) {
                    LogI("render audio data end of stream");
                } else {
                    LogE("missing data may result in noise");
                }
            }
        } else {
            LogE("not found player");
            flag = AudioDataFlag::Error;
            break;
        }
    } while (false);
    return encodeAudioDataResult(flag, requestedSize);
}

namespace slark {

using namespace slark::JNI;

std::string NativeAudioPlayer::create(uint64_t sampleRate, uint8_t channelCount) {
    JNIEnvGuard envGuard(getJavaVM());
    auto playerId = Native_AudioPlayer_createAudioPlayer(envGuard.get(),
                                                         static_cast<jint>(sampleRate),
                                                         channelCount);
    return playerId;
}

uint64_t NativeAudioPlayer::getPlayedTime(const std::string& playerId) {
    JNIEnvGuard envGuard(getJavaVM());
    return Native_AudioPlayer_getPlayedTime(envGuard.get(), playerId);
}

void NativeAudioPlayer::doAction(const std::string& playerId, AudioPlayerAction action) {
    JNIEnvGuard envGuard(getJavaVM());
    Native_AudioPlayer_audioPlayerAction(envGuard.get(), playerId, action);
}

void NativeAudioPlayer::setConfig(const std::string& playerId,
                                  AudioPlayerConfig config,
                                  float value) {
    JNIEnvGuard envGuard(getJavaVM());
    Native_AudioPlayer_setAudioConfig(envGuard.get(), playerId, config, value);
}

} // namespace slark