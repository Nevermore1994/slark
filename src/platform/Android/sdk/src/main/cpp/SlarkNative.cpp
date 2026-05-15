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

extern "C" JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkNativeBridge_logout(JNIEnv* env,
                                            jobject /* this*/,
                                            const std::string& log) {
    JNI::JNICall::callStatic<void>(env, "com/slark/sdk/SlarkNativeBridge", "logout", log);
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