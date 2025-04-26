//
// Created by Nevermore on 2025/4/26.
//
#include "JNIHelper.h"
#include "JNICache.h"
#include "Player.h"
#include "Manager.hpp"
#include "Log.hpp"

namespace slark {

using PlayerManager = Manager<Player>;

}

enum class Action {
    Play,
    Pause,
    Stop,
    Release
};

using namespace slark;
using namespace slark::JNI;

extern "C"
JNIEXPORT jstring JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_createPlayer(JNIEnv *env, jobject /*thiz*/,
                                                                  jstring path, jdouble start,
                                                                  jdouble duration) {
    auto playerParams = std::make_unique<PlayerParams>();
    playerParams->item.path = JNI::FromJVM::toString(env, path);
    playerParams->item.displayStart = start;
    playerParams->item.displayDuration = duration;
    auto context = slark::createEGLContext();
    context->init(nullptr);
    playerParams->mainGLContext = std::move(context);
    auto player = std::make_shared<Player>(std::move(playerParams));
    auto playerId = std::string(player->playerId());
    PlayerManager::shareInstance().add(playerId, player);
    return JNI::ToJVM::toString(env, playerId).get();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_doAction(JNIEnv *env, jobject /*thiz*/,
                                                              jstring jPlayerId, jint actionId) {
    auto playerId = JNI::FromJVM::toString(env, jPlayerId);
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        auto action = static_cast<Action>(actionId);
        switch (action) {
            case Action::Play: {
                player->play();
            }
                break;
            case Action::Pause: {
                player->pause();
            }
                break;
            case Action::Stop: {
                player->stop();
            }
                break;
            case Action::Release: {
                player->stop();
                PlayerManager::shareInstance().remove(playerId);
            }
                break;
        }
    } else {
        LogE("not found player, {}", playerId);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_setLoop(JNIEnv *env, jobject /*thiz*/,
                                                             jstring jPlayerId, jboolean isLoop) {
    auto playerId = JNI::FromJVM::toString(env, jPlayerId);
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->setLoop(static_cast<bool>(isLoop));
    } else {
        LogE("not found player, {}", playerId);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_setMute(JNIEnv *env, jobject /*thiz*/,
                                                             jstring jPlayerId, jboolean isMute) {
    auto playerId = JNI::FromJVM::toString(env, jPlayerId);
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->setMute(static_cast<bool>(isMute));
    } else {
        LogE("not found player, {}", playerId);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_setRenderSize(JNIEnv *env, jobject /*thiz*/,
                                                                   jstring jPlayerId, jint width,
                                                                   jint height) {
    auto playerId = JNI::FromJVM::toString(env, jPlayerId);
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->setRenderSize(width, height);
    } else {
        LogE("not found player, {}", playerId);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_seek(JNIEnv *env, jobject /*thiz*/,
                                                          jstring jPlayerId, jdouble time, jboolean isAccurate) {
    auto playerId = JNI::FromJVM::toString(env, jPlayerId);
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->seek(time, static_cast<bool>(isAccurate));
    } else {
        LogE("not found player, {}", playerId);
    }
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_totalDuration(JNIEnv *env, jobject /*thiz*/,
                                                                   jstring jPlayerId) {
    auto playerId = JNI::FromJVM::toString(env, jPlayerId);
    auto duration = 0.0;
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        duration = static_cast<double>(player->info().duration);
    } else {
        LogE("not found player, {}", playerId);
    }
    return duration;
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_currentPlayedTime(JNIEnv *env, jobject /*thiz*/,
                                                                       jstring jPlayerId) {
    auto playerId = JNI::FromJVM::toString(env, jPlayerId);
    auto playedTime = 0.0;
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        playedTime = static_cast<double>(player->currentPlayedTime());
    } else {
        LogE("not found player, {}", playerId);
    }
    return playedTime;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_setVolume(JNIEnv *env, jobject /*thiz*/,
                                                               jstring jPlayerId, jfloat volume) {
    auto playerId = JNI::FromJVM::toString(env, jPlayerId);
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->setVolume(volume);
    } else {
        LogE("not found player, {}", playerId);
    }
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_state(JNIEnv *env, jobject  /*thiz*/,
                                                           jstring jPlayerId) {
    constexpr std::string_view kPlayerStateClass = "com/slark/api/SlarkPlayerState";
    auto playerId = FromJVM::toString(env, jPlayerId);
    std::string_view fieldView = "Unknown";
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        constexpr std::string_view playerStateNames[] = {
            "Unknown",
            "Initializing",
            "Buffering",
            "Ready",
            "Playing",
            "Pause",
            "Stop",
            "Error",
            "Completed",
        };

        auto state = player->state();
        fieldView = playerStateNames[static_cast<size_t>(state)];
    } else {
        LogE("not found player, {}", playerId);
    }
    auto stateValue = JNICache::shareInstance().getEnumField(env, kPlayerStateClass, fieldView);
    return stateValue.get();
}