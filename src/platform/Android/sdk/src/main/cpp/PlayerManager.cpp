//
// Created by Nevermore on 2025/4/26.
//
#include "JNIHelper.h"
#include "JNICache.h"
#include "JNIEnvGuard.hpp"
#include "Player.h"
#include "Manager.hpp"
#include "Log.hpp"
#include "JNISignature.h"
#include "GLContextManager.h"
#include "Manager.hpp"
#include "VideoRender.h"

namespace slark {

class PlayerObserver : public IPlayerObserver {
public:
    PlayerObserver() = default;

    void notifyPlayedTime(
        std::string_view playerId,
        double time
    ) override {
        using namespace slark::JNI;
        JNIEnvGuard jniGuard(getJavaVM());
        auto jPlayerId = JNI::ToJVM::toString(
            jniGuard.get(),
            playerId
        );
        auto jTime = static_cast<jdouble>(time);
        auto playerClass = JNICache::shareInstance().getClass(
            jniGuard.get(),
            kPlayerManagerClass
        );
        if (!playerClass) {
            LogE("not found player manager class");
            return;
        }
        auto methodSignature = JNI::makeJNISignature(
            JNI::Void,
            JNI::String,
            JNI::Double
        );
        auto methodId = JNICache::shareInstance().getStaticMethodId(
            playerClass,
            "notifyPlayerTime",
            methodSignature
        );
        if (!methodId) {
            LogE("not found method");
            return;
        }
        jniGuard.get()->CallStaticVoidMethod(
            playerClass.get(),
            methodId.get(),
            jPlayerId.detach(),
            jTime
        );
    }

    void notifyPlayerState(
        std::string_view playerId,
        PlayerState state
    ) override {
        using namespace slark::JNI;
        JNIEnvGuard jniGuard(getJavaVM());
        auto jPlayerId = JNI::ToJVM::toString(
            jniGuard.get(),
            playerId
        );
        auto playerClass = JNICache::shareInstance().getClass(
            jniGuard.get(),
            kPlayerManagerClass
        );
        if (!playerClass) {
            LogE("not found player manager class");
            return;
        }

        auto methodSignature = JNI::makeJNISignature(
            JNI::Void,
            JNI::String,
            JNI::makeObject(kPlayerStateClass));
        auto methodId = JNICache::shareInstance().getStaticMethodId(
            playerClass,
            "notifyPlayerState",
            methodSignature
        );
        if (!methodId) {
            LogE("not found method");
            return;
        }
        std::string_view fieldView = "Unknown";
        static std::vector<std::string_view> stateNames = {
            "Unknown",
            "Initializing",
            "Prepared",
            "Buffering",
            "Ready",
            "Playing",
            "Pause",
            "Stop",
            "Error",
            "Completed"
        };
        if (static_cast<uint8_t>(state) < stateNames.size()) {
            fieldView = stateNames[static_cast<uint8_t>(state)];
        }
        auto stateEnum = JNICache::shareInstance().getEnumField(
            jniGuard.get(),
            kPlayerStateClass,
            fieldView
        );
        jniGuard.get()->CallStaticVoidMethod(
            playerClass.get(),
            methodId.get(),
            jPlayerId.detach(),
            stateEnum.detach()
        );
    }

    void notifyPlayerEvent(
        std::string_view playerId,
        slark::PlayerEvent event,
        std::string value
    ) override {
        using namespace slark::JNI;
        JNIEnvGuard jniGuard(getJavaVM());
        auto jPlayerId = JNI::ToJVM::toString(
            jniGuard.get(),
            playerId
        );
        auto playerClass = JNICache::shareInstance().getClass(
            jniGuard.get(),
            kPlayerManagerClass
        );
        if (!playerClass) {
            LogE("not found player manager class");
            return;
        }

        auto methodSignature = JNI::makeJNISignature(
            JNI::Void,
            JNI::String,
            JNI::makeObject(kPlayerEventClass),
            JNI::String
        );
        auto methodId = JNICache::shareInstance().getStaticMethodId(
            playerClass,
            "notifyPlayerEvent",
            methodSignature
        );
        if (!methodId) {
            LogE("not found method");
            return;
        }
        std::string_view fieldView = "Unknown";
        static std::vector<std::string_view> eventNames = {
            "FirstFrameRendered",
            "SeekDone",
            "PlayEnd",
            "UpdateCacheTime",
            "OnError"
        };
        if (static_cast<uint8_t>(event) < eventNames.size()) {
            fieldView = eventNames[static_cast<uint8_t>(event)];
        }
        auto eventEnum = JNICache::shareInstance().getEnumField(
            jniGuard.get(),
            kPlayerEventClass,
            fieldView
        );
        auto jValue = JNI::ToJVM::toString(
            jniGuard.get(),
            value
        );
        jniGuard.get()->CallStaticVoidMethod(
            playerClass.get(),
            methodId.get(),
            jPlayerId.detach(),
            eventEnum.detach(),
            jValue.detach()
        );
    }

private:
    const std::string_view kPlayerManagerClass = "com/slark/sdk/SlarkPlayerManager";
    const std::string_view kPlayerStateClass = "com/slark/api/SlarkPlayerState";
    const std::string_view kPlayerEventClass = "com/slark/api/SlarkPlayerEvent";
};

using PlayerManager = Manager<Player>;
using PlayerObserverManager = Manager<PlayerObserver>;
}

enum class Action {
    Prepare = 0,
    Play,
    Pause,
    Stop,
    Release
};

using namespace slark;
using namespace slark::JNI;

extern "C"
JNIEXPORT jstring JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_createPlayer(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring path,
    jdouble start,
    jdouble duration
) {
    auto playerParams = std::make_unique<PlayerParams>();
    playerParams->item
        .path = JNI::FromJVM::toString(
        env,
        path
    );
    playerParams->item
        .displayStart = start;
    playerParams->item
        .displayDuration = duration;
    playerParams->mainGLContext = createMainGLContext();
    auto mainContext = playerParams->mainGLContext;
    auto player = std::make_shared<Player>(std::move(playerParams));
    auto playerId = std::string(player->playerId());

    //set observer
    auto observer = std::make_shared<PlayerObserver>();
    player->addObserver(observer);
    PlayerManager::shareInstance().add(
        playerId,
        player
    );
    PlayerObserverManager::shareInstance().add(
        playerId,
        observer
    );

    //set render
    auto render = std::make_shared<VideoRender>();
    render->setPlayerId(playerId);
    player->setRenderImpl(render);
    VideoRenderManager::shareInstance().add(
        playerId,
        render
    );
    return JNI::ToJVM::toString(
        env,
        playerId
    ).detach();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_doAction(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring jPlayerId,
    jint actionId
) {
    auto playerId = JNI::FromJVM::toString(
        env,
        jPlayerId
    );
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        auto action = static_cast<Action>(actionId);
        switch (action) {
            case Action::Prepare: {
                player->prepare();
            }
                break;
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
                PlayerObserverManager::shareInstance().remove(playerId);
                VideoRenderManager::shareInstance().remove(playerId);
                PlayerManager::shareInstance().remove(playerId);
            }
                break;
        }
    } else {
        LogE("not found player, {}",
             playerId);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_setLoop(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring jPlayerId,
    jboolean isLoop
) {
    auto playerId = JNI::FromJVM::toString(
        env,
        jPlayerId
    );
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->setLoop(static_cast<bool>(isLoop));
    } else {
        LogE("not found player, {}",
             playerId);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_setMute(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring jPlayerId,
    jboolean isMute
) {
    auto playerId = JNI::FromJVM::toString(
        env,
        jPlayerId
    );
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->setMute(static_cast<bool>(isMute));
    } else {
        LogE("not found player, {}",
             playerId);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_setRenderSize(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring jPlayerId,
    jint width,
    jint height
) {
    auto playerId = JNI::FromJVM::toString(
        env,
        jPlayerId
    );
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->setRenderSize(
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height));
    } else {
        LogE("not found player, {}",
             playerId);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_seek(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring jPlayerId,
    jdouble time,
    jboolean isAccurate
) {
    auto playerId = JNI::FromJVM::toString(
        env,
        jPlayerId
    );
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->seek(time, static_cast<bool>(isAccurate));
    } else {
        LogE("not found player, {}",
             playerId);
    }
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_totalDuration(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring jPlayerId
) {
    auto playerId = JNI::FromJVM::toString(
        env,
        jPlayerId
    );
    auto duration = 0.0;
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        duration = static_cast<double>(player->info()
            .duration);
    } else {
        LogE("not found player, {}",
             playerId);
    }
    return duration;
}

extern "C"
JNIEXPORT jdouble JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_currentPlayedTime(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring jPlayerId
) {
    auto playerId = JNI::FromJVM::toString(
        env,
        jPlayerId
    );
    auto playedTime = 0.0;
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        playedTime = static_cast<double>(player->currentPlayedTime());
    } else {
        LogE("not found player, {}",
             playerId);
    }
    return playedTime;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_setVolume(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring jPlayerId,
    jfloat volume
) {
    auto playerId = JNI::FromJVM::toString(
        env,
        jPlayerId
    );
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        player->setVolume(volume);
    } else {
        LogE("not found player, {}",
             playerId);
    }
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_slark_sdk_SlarkPlayerManager_00024Companion_state(
    JNIEnv *env,
    jobject  /*thiz*/,
    jstring jPlayerId
) {
    constexpr std::string_view kPlayerStateClass = "com/slark/api/SlarkPlayerState";
    auto playerId = FromJVM::toString(
        env,
        jPlayerId
    );
    std::string_view fieldView = "Unknown";
    if (auto player = PlayerManager::shareInstance().find(playerId)) {
        constexpr std::string_view playerStateNames[] = {
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

        auto state = player->state();
        fieldView = playerStateNames[static_cast<size_t>(state)];
    } else {
        LogE("not found player, {}",
             playerId);
    }
    auto stateValue = JNICache::shareInstance().getEnumField(
        env,
        kPlayerStateClass,
        fieldView
    );
    return stateValue.detach();
}