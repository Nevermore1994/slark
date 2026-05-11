#include "PlayerManagerInternal.h"

#include "JNICache.h"
#include "JNICall.h"
#include "JNIEnvGuard.h"
#include "Log.hpp"
#include "PlayerStateNames.h"
#include "VideoDecodeResource.h"

#include <thread>

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
    auto stateIdx = static_cast<size_t>(state);
    if (stateIdx >= kPlayerStateCount) {
        LogE("notifyPlayerState: invalid state index {}", stateIdx);
        return;
    }
    auto stateEnum = JNICache::instance().getEnumField(jniGuard.get(),
                                                       kPlayerStateClass,
                                                       kPlayerStateNames[stateIdx]);
    JNICall::callStatic<void>(jniGuard.get(),
                              kPlayerManagerClass,
                              "notifyPlayerState",
                              std::string(playerId),
                              JObjectWithSig(std::move(stateEnum), makeObject(kPlayerStateClass)));
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
    // 使用 std::thread + detach 真正异步，避免 std::async future 析构时阻塞主线程
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

} // namespace slark
