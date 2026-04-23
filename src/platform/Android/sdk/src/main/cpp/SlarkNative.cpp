//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "JNICache.h"
#include "JNIEnvGuard.h"
#include "JNIMarshal.h"
#include "Log.hpp"

#include <string>

using namespace slark;
using namespace slark::JNI;

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkNativeBridge_logout(JNIEnv* env,
                                            jobject /* this*/,
                                            const std::string& log) {
    auto bridgeClass = JNICache::instance().getClass(env, "com/slark/sdk/SlarkNativeBridge");
    if (!bridgeClass) {
        LogE("Failed to find SlarkNativeBridge class");
        return;
    }
    auto methodId =
        JNICache::instance().getStaticMethod(bridgeClass, "logout", "(Ljava/lang/String;)V");
    if (!methodId) {
        LogE("Failed to find logout method in SlarkNativeBridge class");
        return;
    }
    auto message = ToJVM::str(env, log);
    if (!message) {
        LogE("Failed to convert log message to jstring");
        return;
    }
    env->CallStaticVoidMethod(bridgeClass.get(), methodId.get(), message.get());
}

namespace slark {

void printLog(const std::string& logStr) {
    JNIEnvGuard guard(JNI::getJavaVM());
    if (!guard) {
        return;
    }
    Java_com_slark_sdk_SlarkNativeBridge_logout(guard.get(), nullptr, logStr);
}

} // namespace slark