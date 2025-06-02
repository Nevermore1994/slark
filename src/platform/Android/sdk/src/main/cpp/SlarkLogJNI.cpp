#include "JNIHelper.h"
#include "Log.hpp"

extern "C" {

JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkLog_00024Companion_nativeLog(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring message
) {
    using namespace slark;
    const char *nativeMessage = env->GetStringUTFChars(
        message,
        nullptr
    );
    outputLog(
        LogType::Record,
        "{}",
        nativeMessage
    );
    env->ReleaseStringUTFChars(
        message,
        nativeMessage
    );
}

}
