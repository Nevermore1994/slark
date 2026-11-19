//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include <optional>
#include <string>
#include <string_view>

#include "GLContextManager.h"
#include "JNICache.h"
#include "JNICall.h"
#include "JNIEnvGuard.h"
#include "JNIFieldReader.h"
#include "JNIMarshal.h"
#include "Log.hpp"
#include "Manager.hpp"
#include "Player.h"
#include "PlayerManagerInternal.h"
#include "PlayerStateNames.h"
#include "VideoRender.h"

namespace slark {

using PlayerManager = Manager<Player>;
using PlayerObserverManager = Manager<PlayerObserver>;

}  // namespace slark

using namespace slark;
using namespace slark::JNI;

namespace {

constexpr std::string_view kSourceSignature = "Lcom/slark/api/SlarkFlowConfig$Source;";
constexpr std::string_view kBufferingSignature = "Lcom/slark/api/SlarkFlowConfig$Buffering;";
constexpr std::string_view kDnsSignature = "Lcom/slark/api/SlarkFlowConfig$Dns;";

void raiseIllegalArgument(JNIEnv* env, std::string_view message) {
    if (!env || env->ExceptionCheck()) {
        return;
    }
    ClassReference errorClass(env, env->FindClass("java/lang/IllegalArgumentException"));
    if (errorClass) {
        env->ThrowNew(errorClass.get(), std::string(message).c_str());
    }
}

template <typename Group, typename... Fields>
[[nodiscard]] JNI::GroupRead readFlowGroup(JNIEnv* env,
                                           jobject object,
                                           jclass cls,
                                           std::string_view name,
                                           std::string_view signature,
                                           Group& group,
                                           Fields... fields) {
    const auto value = JNI::readObjectField(env, object, cls, name, signature);
    if (!value) {
        return std::unexpected(name);
    }
    if (!JNI::readFields(env, value.get(), group, fields...)) {
        return std::unexpected(name);
    }
    return JNI::GroupRead{};
}

[[nodiscard]] JNI::GroupRead readSource(JNIEnv* env,
                                        jobject object,
                                        jclass cls,
                                        SourceWatermarkConfig& group) {
    return readFlowGroup(env, object, cls, "source", kSourceSignature, group,
                         field("lowWatermarkSec", &SourceWatermarkConfig::lowWatermarkSec),
                         field("highWatermarkSec", &SourceWatermarkConfig::highWatermarkSec));
}

[[nodiscard]] JNI::GroupRead readBuffering(JNIEnv* env,
                                           jobject object,
                                           jclass cls,
                                           BufferingConfig& group) {
    return readFlowGroup(env, object, cls, "buffering", kBufferingSignature, group,
                         field("startBufferSec", &BufferingConfig::startBufferSec),
                         field("rebufferEnterSec", &BufferingConfig::rebufferEnterSec),
                         field("rebufferExitSec", &BufferingConfig::rebufferExitSec));
}

[[nodiscard]] JNI::GroupRead readDnsGroup(JNIEnv* env, jobject dns, DnsConfig& group) {
    ClassReference dnsClass(env, env->GetObjectClass(dns));
    if (!dnsClass ||
        !JNI::readStringList(env, dns, dnsClass.get(), "preResolveUrls", group.preResolveUrls) ||
        !JNI::readFields(env, dns, group, field("cacheTtlSec", &DnsConfig::cacheTtlSec))) {
        return std::unexpected("dns");
    }
    return JNI::GroupRead{};
}

[[nodiscard]] JNI::GroupRead readDns(JNIEnv* env, jobject object, jclass cls, DnsConfig& group) {
    const auto value = JNI::readObjectField(env, object, cls, "dns", kDnsSignature);
    if (!value) {
        return std::unexpected("dns");
    }
    return readDnsGroup(env, value.get(), group);
}

[[nodiscard]] JNI::GroupRead readFlowConfig(JNIEnv* env, jobject object, FlowConfig& config) {
    if (!env) {
        return std::unexpected("flowConfig");
    }
    if (!object) {
        return std::unexpected("flowConfig");
    }
    if (env->ExceptionCheck()) {
        return std::unexpected("flowConfig");
    }
    ClassReference cls(env, env->GetObjectClass(object));
    if (!cls) {
        return std::unexpected("flowConfig");
    }
    return JNI::readGroups(
        [&] {
            return readSource(env, object, cls.get(), config.source);
        },
        [&] {
            return readBuffering(env, object, cls.get(), config.buffering);
        },
        [&] {
            return readDns(env, object, cls.get(), config.dns);
        });
}

jstring createPlayer(JNIEnv* env,
                     jstring path,
                     jdouble start,
                     jdouble duration,
                     std::optional<FlowConfig> flowConfig) {
    auto playerParams = std::make_unique<PlayerParams>();
    playerParams->setting.flowConfig = std::move(flowConfig);
    playerParams->item.path = JNI::FromJVM::str(env, path);
    playerParams->item.displayStart = start;
    playerParams->item.displayDuration = duration;
    playerParams->mainGLContext = createMainGLContext();
    auto mainContext = playerParams->mainGLContext;
    auto player = std::make_shared<Player>(std::move(playerParams));
    auto playerId = std::string(player->playerId());
    asyncCreateDecoderResource(playerId);

    auto observer = std::make_shared<PlayerObserver>();
    player->addObserver(observer);
    PlayerManager::shareInstance().add(playerId, player);
    PlayerObserverManager::shareInstance().add(playerId, observer);

    auto render = std::make_shared<VideoRender>();
    render->setPlayerId(playerId);
    player->setRenderImpl(render);
    VideoRenderManager::shareInstance().add(playerId, render);
    return JNI::ToJVM::str(env, playerId).detach();
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_createPlayer(JNIEnv* env,
                                                                  jobject /*thiz*/,
                                                                  jstring path,
                                                                  jdouble start,
                                                                  jdouble duration) {
    return createPlayer(env, path, start, duration, std::nullopt);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_createPlayerWithFlowConfig(JNIEnv* env,
                                                                                jobject /*thiz*/,
                                                                                jstring path,
                                                                                jdouble start,
                                                                                jdouble duration,
                                                                                jobject config) {
    FlowConfig flowConfig;
    if (const auto result = readFlowConfig(env, config, flowConfig); !result.has_value()) {
        raiseIllegalArgument(env, std::string(result.error()).append(" must not be null"));
        return nullptr;
    }
    return createPlayer(env, path, start, duration, flowConfig);
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_preResolve(JNIEnv* env,
                                                                jobject /*thiz*/,
                                                                jobject dns) {
    DnsConfig config;
    if (!dns || !readDnsGroup(env, dns, config).has_value()) {
        raiseIllegalArgument(env, "dns must not be null");
        return;
    }
    Player::preResolve(config);
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_doAction(JNIEnv* env,
                                                              jobject /*thiz*/,
                                                              jstring jPlayerId,
                                                              jint actionId) {
    auto playerId = JNI::FromJVM::str(env, jPlayerId);
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        auto action = static_cast<Action>(actionId);
        switch (action) {
            case Action::Prepare: {
                player->prepare();
            } break;
            case Action::Play: {
                player->play();
            } break;
            case Action::Pause: {
                player->pause();
            } break;
            case Action::Release: {
                player->stop();
                PlayerObserverManager::shareInstance().remove(playerId);
                VideoRenderManager::shareInstance().remove(playerId);
                PlayerManager::shareInstance().remove(playerId);
            } break;
        }
    } else {
        LogE("not found player, {}", playerId);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_setLoop(JNIEnv* env,
                                                             jobject /*thiz*/,
                                                             jstring jPlayerId,
                                                             jboolean isLoop) {
    auto playerId = JNI::FromJVM::str(env, jPlayerId);
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->setLoop(static_cast<bool>(isLoop));
    } else {
        LogE("not found player, {}", playerId);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_setMute(JNIEnv* env,
                                                             jobject /*thiz*/,
                                                             jstring jPlayerId,
                                                             jboolean isMute) {
    auto playerId = JNI::FromJVM::str(env, jPlayerId);
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->setMute(static_cast<bool>(isMute));
    } else {
        LogE("not found player, {}", playerId);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_seek(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jstring jPlayerId,
                                                          jdouble time,
                                                          jboolean isAccurate) {
    auto playerId = JNI::FromJVM::str(env, jPlayerId);
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->seek(time, static_cast<bool>(isAccurate));
    } else {
        LogE("not found player, {}", playerId);
    }
}

extern "C" JNIEXPORT jdouble JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_totalDuration(JNIEnv* env,
                                                                   jobject /*thiz*/,
                                                                   jstring jPlayerId) {
    auto playerId = JNI::FromJVM::str(env, jPlayerId);
    auto duration = 0.0;
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        duration = static_cast<double>(player->info().duration);
    } else {
        LogE("not found player, {}", playerId);
    }
    return duration;
}

extern "C" JNIEXPORT jdouble JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_currentPlayedTime(JNIEnv* env,
                                                                       jobject /*thiz*/,
                                                                       jstring jPlayerId) {
    auto playerId = JNI::FromJVM::str(env, jPlayerId);
    auto playedTime = 0.0;
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        playedTime = static_cast<double>(player->currentPlayedTime());
    } else {
        LogE("not found player, {}", playerId);
    }
    return playedTime;
}

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_setVolume(JNIEnv* env,
                                                               jobject /*thiz*/,
                                                               jstring jPlayerId,
                                                               jfloat volume) {
    auto playerId = JNI::FromJVM::str(env, jPlayerId);
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->setVolume(volume);
    } else {
        LogE("not found player, {}", playerId);
    }
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_state(JNIEnv* env,
                                                           jobject /*thiz*/,
                                                           jstring jPlayerId) {
    constexpr std::string_view kPlayerStateClass = JavaClass::SlarkPlayerState;
    auto playerId = FromJVM::str(env, jPlayerId);
    std::string_view fieldView = "Unknown";
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        auto stateIdx = static_cast<size_t>(player->state());
        fieldView = stateIdx < kPlayerStateCount ? kPlayerStateNames[stateIdx] : "Unknown";
    } else {
        LogE("not found player, {}", playerId);
    }
    auto stateValue = JNICache::instance().getEnumField(env, kPlayerStateClass, fieldView);
    return stateValue.detach();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_hasVideo(JNIEnv* env,
                                                              jobject /*thiz*/,
                                                              jstring jPlayerId) {
    auto playerId = FromJVM::str(env, jPlayerId);
    auto hasVideo = false;
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        hasVideo = player->info().hasVideo;
    } else {
        LogE("not found player, {}", playerId);
    }
    return hasVideo;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_isSeekable(JNIEnv* env,
                                                                jobject /*thiz*/,
                                                                jstring jPlayerId) {
    auto playerId = FromJVM::str(env, jPlayerId);
    auto isSeekable = false;
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        isSeekable = player->info().isSeekable;
    } else {
        LogE("not found player, {}", playerId);
    }
    return isSeekable;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_debugSnapshot(JNIEnv* env,
                                                                   jobject /*thiz*/,
                                                                   jstring jPlayerId) {
    auto playerId = FromJVM::str(env, jPlayerId);
    std::string snapshot;
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        snapshot = player->debugSnapshot();
    } else {
        LogE("not found player, {}", playerId);
    }
    return JNI::ToJVM::str(env, snapshot).detach();
}