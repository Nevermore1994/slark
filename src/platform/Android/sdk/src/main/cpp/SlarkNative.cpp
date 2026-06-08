//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "JNICache.h"
#include "JNICall.h"
#include "JNIEnvGuard.h"
#include "JNIMarshal.h"
#include "Log.hpp"

#include <string>

using namespace slark;
using namespace slark::JNI;

extern "C" JNIEXPORT void JNICALL Java_com_slark_sdk_SlarkNativeBridge_logout(JNIEnv* env,
                                                                              jobject /* this */,
                                                                              jint level,
                                                                              jstring message) {
    JNI::JNICall::callStatic<void>(env,
                                   "com/slark/sdk/SlarkNativeBridge",
                                   "logout",
                                   level,
                                   message);
}

namespace slark {

void printLog(LogType type, const std::string& logStr) {
    JNIEnvGuard guard(JNI::getJavaVM());
    if (!guard) {
        return;
    }
    auto* env = guard.get();
    auto jMessage = JNI::ToJVM::str(env, logStr);
    Java_com_slark_sdk_SlarkNativeBridge_logout(env,
                                                nullptr,
                                                static_cast<jint>(type),
                                                jMessage.get());
}

} // namespace slark