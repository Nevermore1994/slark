//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "PlayerManagerInternal.h"

#include <thread>

#include "JNICache.h"
#include "JNICall.h"
#include "JNIEnvGuard.h"
#include "Log.hpp"
#include "PlayerStateNames.h"
#include "VideoDecodeResource.h"

namespace slark {

void PlayerObserver::notifyPlayedTime(std::string_view playerId, double time) {
    using namespace slark::JNI;
    JNIEnvGuard jniGuard(getJavaVM());
    JNICall::callStatic<void>(jniGuard.get(),
                              kPlayerManagerClass,
                              "notifyPlayerTime",
                              std::string(playerId),
                              time);
}

void PlayerObserver::notifyPlayerState(std::string_view playerId, PlayerState state) {
    using namespace slark::JNI;
    JNIEnvGuard jniGuard(getJavaVM());
    auto* env = jniGuard.get();
    auto stateIdx = static_cast<size_t>(state);
    if (stateIdx >= kPlayerStateCount) {
        LogE("notifyPlayerState: invalid state index {}", stateIdx);
        return;
    }
    auto stateEnum =
        JNICache::instance().getEnumField(env, kPlayerStateClass, kPlayerStateNames[stateIdx]);
    JNICall::callStatic<void>(env,
                              kPlayerManagerClass,
                              "notifyPlayerState",
                              std::string(playerId),
                              JObjectWithSig(std::move(stateEnum), makeObject(kPlayerStateClass)));
    if (state == PlayerState::Prepared || state == PlayerState::Playing) {
        JNICall::callStatic<void>(env,
                                  kPlayerManagerClass,
                                  "startRendering",
                                  std::string(playerId));
    } else if (state == PlayerState::Pause || state == PlayerState::Completed ||
               state == PlayerState::Stop || state == PlayerState::Error) {
        JNICall::callStatic<void>(env, kPlayerManagerClass, "stopRendering", std::string(playerId));
    }
}

void PlayerObserver::notifyPlayerEvent(std::string_view playerId,
                                       slark::PlayerEvent event,
                                       std::string value) {
    using namespace slark::JNI;
    JNIEnvGuard jniGuard(getJavaVM());
    auto eventIdx = static_cast<size_t>(event);
    if (eventIdx >= kPlayerEventCount) {
        LogE("notifyPlayerEvent: invalid event index {}", eventIdx);
        return;
    }
    auto eventEnum = JNICache::instance().getEnumField(jniGuard.get(),
                                                       kPlayerEventClass,
                                                       kPlayerEventNames[eventIdx]);
    JNICall::callStatic<void>(jniGuard.get(),
                              kPlayerManagerClass,
                              "notifyPlayerEvent",
                              std::string(playerId),
                              JObjectWithSig(std::move(eventEnum), makeObject(kPlayerEventClass)),
                              value);
}

void asyncCreateDecoderResource(const std::string& playerId) noexcept {
    std::thread([playerId]() {
        auto resource = std::make_shared<VideoDecodeResource>(playerId);
        resource->init();
        if (!resource->isValid()) {
            LogE("asyncCreateDecoderResource: init failed for playerId:{}", playerId);
            return;
        }
        if (!VideoDecodeResourceManager::shareInstance().find(playerId)) {
            VideoDecodeResourceManager::shareInstance().add(playerId, resource);
        }
    }).detach();
}

}  // namespace slark
