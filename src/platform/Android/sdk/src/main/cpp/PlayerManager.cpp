//
// Created by Nevermore on 2025/4/26.
//
#include "GLContextManager.h"
#include "JNICache.h"
#include "JNICall.h"
#include "JNIEnvGuard.h"
#include "JNIMarshal.h"
#include "JNISignature.h"
#include "Log.hpp"
#include "Manager.hpp"
#include "Player.h"
#include "PlayerObserver.h"
#include "VideoDecodeResource.h"
#include "VideoRender.h"

namespace slark {

namespace {

constexpr std::string_view kPlayerStateNames[] = {
    "Unknown",
    "Initializing",
    "Prepared",
    "Buffering",
    "Ready",
    "Playing",
    "Pause",
    "Stop",
    "Error",
    "Completed",
};

constexpr std::string_view kPlayerEventNames[] = {
    "FirstFrameRendered",
    "SeekDone",
    "PlayEnd",
    "UpdateCacheTime",
    "OnError",
};

template <typename T, size_t N>
constexpr std::string_view enumToName(const std::string_view (&names)[N], T value) noexcept {
    auto idx = static_cast<size_t>(value);
    return idx < N ? names[idx] : "Unknown";
}

} // namespace

class PlayerObserver : public IPlayerObserver {
public:
    PlayerObserver() = default;

    void notifyPlayedTime(std::string_view playerId, double time) override {
        using namespace slark::JNI;
        JNIEnvGuard jniGuard(getJavaVM());
        JNI::JNICall::callStatic<void>(jniGuard.get(),
                                       kPlayerManagerClass,
                                       "notifyPlayerTime",
                                       std::string(playerId),
                                       time);
    }

    void notifyPlayerState(std::string_view playerId, PlayerState state) override {
        using namespace slark::JNI;
        JNIEnvGuard jniGuard(getJavaVM());
        auto env = jniGuard.get();
        auto fieldName = enumToName(kPlayerStateNames, state);
        auto stateEnum = JNICache::instance().getEnumField(env, kPlayerStateClass, fieldName);
        JNI::JNICall::callStatic<void>(
            env,
            kPlayerManagerClass,
            "notifyPlayerState",
            JNI::JObjectWithSig(ToJVM::str(env, playerId), JNI::Sig::String),
            JNI::JObjectWithSig(std::move(stateEnum), JNI::makeObject(kPlayerStateClass)));
    }

    void notifyPlayerEvent(std::string_view playerId,
                           slark::PlayerEvent event,
                           std::string value) override {
        using namespace slark::JNI;
        JNIEnvGuard jniGuard(getJavaVM());
        auto env = jniGuard.get();
        auto fieldName = enumToName(kPlayerEventNames, event);
        auto eventEnum = JNICache::instance().getEnumField(env, kPlayerEventClass, fieldName);
        JNI::JNICall::callStatic<void>(
            env,
            kPlayerManagerClass,
            "notifyPlayerEvent",
            JNI::JObjectWithSig(ToJVM::str(env, playerId), JNI::Sig::String),
            JNI::JObjectWithSig(std::move(eventEnum), JNI::makeObject(kPlayerEventClass)),
            std::move(value));
    }

private:
    const std::string_view kPlayerManagerClass = "com/slark/sdk/SlarkPlayerManager";
    const std::string_view kPlayerStateClass = "com/slark/api/SlarkPlayerState";
    const std::string_view kPlayerEventClass = "com/slark/api/SlarkPlayerEvent";
};

using PlayerManager = Manager<Player>;
using PlayerObserverManager = Manager<PlayerObserver>;
} // namespace slark

enum class Action { Prepare = 0, Play, Pause, Release };

using namespace slark;
using namespace slark::JNI;

void asyncCreateDecoderResource(const std::string& playerId) noexcept {
    std::thread([playerId]() {
        auto resource = std::make_shared<VideoDecodeResource>(playerId);
        resource->init();
        VideoDecodeResourceManager::shareInstance().add(playerId, resource);
    }).detach();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_createPlayer(JNIEnv* env,
                                                                  jobject /*thiz*/,
                                                                  jstring path,
                                                                  jdouble start,
                                                                  jdouble duration) {
    auto playerParams = std::make_unique<PlayerParams>();
    playerParams->item.path = JNI::FromJVM::str(env, path);
    playerParams->item.displayStart = start;
    playerParams->item.displayDuration = duration;
    playerParams->mainGLContext = createMainGLContext();
    auto mainContext = playerParams->mainGLContext;
    auto player = std::make_shared<Player>(std::move(playerParams));
    auto playerId = std::string(player->playerId());
    asyncCreateDecoderResource(playerId);

    // set observer
    auto observer = std::make_shared<PlayerObserver>();
    player->addObserver(observer);
    PlayerManager::shareInstance().add(playerId, player);
    PlayerObserverManager::shareInstance().add(playerId, observer);

    // set render
    auto render = std::make_shared<VideoRender>();
    render->setPlayerId(playerId);
    player->setRenderImpl(render);
    VideoRenderManager::shareInstance().add(playerId, render);
    return JNI::ToJVM::str(env, playerId).detach();
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
    constexpr std::string_view kPlayerStateClass = "com/slark/api/SlarkPlayerState";
    auto playerId = FromJVM::str(env, jPlayerId);
    std::string_view fieldView = "Unknown";
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        fieldView = enumToName(kPlayerStateNames, player->state());
    } else {
        LogE("not found player, {}", playerId);
    }
    auto stateValue = JNICache::instance().getEnumField(env, kPlayerStateClass, fieldView);
    return stateValue.detach();
}