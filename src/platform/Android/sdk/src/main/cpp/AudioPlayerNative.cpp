//
// Created by Nevermore on 2025/2/24.
//

#include "AudioPlayerNative.h"

#include "AudioRender.h"
#include "JNICache.h"
#include "JNIEnvGuard.h"
#include "JNIMarshal.h"
#include "JNISignature.h"
#include "Log.hpp"

using namespace slark;
using namespace slark::JNI;

constexpr std::string_view kAudioPlayerClass = "com/slark/sdk/AudioPlayer";

std::string Native_AudioPlayer_createAudioPlayer(JNIEnv* env, jint sampleRate, jint channelCount) {
    auto audioPlayerClass = JNICache::instance().getClass(env, kAudioPlayerClass);
    if (!audioPlayerClass) {
        LogE("create player failed, not found class");
        return "";
    }
    auto methodSignature = JNI::makeSignature(JNI::Sig::String, JNI::Sig::Int, JNI::Sig::Int);
    auto createMethod = JNICache::instance().getStaticMethod(audioPlayerClass,
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
    auto playerId = JNI::FromJVM::str(env, jPlayerId);
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
    auto audioPlayerClass = JNICache::instance().getClass(env, kAudioPlayerClass);
    if (!audioPlayerClass) {
        LogE("create player failed, not found audio player class");
        return;
    }

    auto jPlayerId = ToJVM::str(env, playerId);
    auto signature =
        JNI::makeSignature(JNI::Sig::Void, JNI::Sig::String, JNI::makeObject(kActionClass));
    auto actionMethodId =
        JNICache::instance().getStaticMethod(audioPlayerClass, "audioPlayerAction", signature);
    env->CallStaticVoidMethod(audioPlayerClass.get(),
                              actionMethodId.get(),
                              jPlayerId.detach(),
                              enumValue.detach());
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
    auto audioPlayerClass = JNICache::instance().getClass(env, kAudioPlayerClass);
    if (!audioPlayerClass) {
        LogE("create player failed, not found audio player class");
        return;
    }

    auto jPlayerId = ToJVM::str(env, playerId);
    auto signature = JNI::makeSignature(JNI::Sig::Void,
                                        JNI::Sig::String,
                                        JNI::makeObject(kConfigClass),
                                        JNI::Sig::Float);
    auto methodId =
        JNICache::instance().getStaticMethod(audioPlayerClass, "audioPlayerConfig", signature);
    if (!methodId) {
        LogE("not found method!");
        return;
    }
    env->CallStaticVoidMethod(audioPlayerClass.get(),
                              methodId.get(),
                              jPlayerId.detach(),
                              enumValue.detach(),
                              static_cast<jfloat>(value));
}

uint64_t Native_AudioPlayer_getPlayedTime(JNIEnv* env, const std::string& playerId) {
    auto audioPlayerClass = JNICache::instance().getClass(env, kAudioPlayerClass);
    if (!audioPlayerClass) {
        LogE("not found audio player class");
        return 0;
    }
    auto signature = JNI::makeSignature(JNI::Sig::Long, JNI::Sig::String);
    auto method =
        JNICache::instance().getStaticMethod(audioPlayerClass, "getPlayedTime", signature);
    if (!method) {
        LogE("not found audio player method");
        return 0;
    }
    auto jPlayerId = ToJVM::str(env, playerId);
    auto playedTime =
        env->CallStaticLongMethod(audioPlayerClass.get(), method.get(), jPlayerId.detach());
    return static_cast<uint64_t>(playedTime);
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