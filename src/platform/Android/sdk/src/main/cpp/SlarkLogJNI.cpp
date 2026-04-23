#include "JNIMarshal.h"
#include "Log.hpp"

extern "C" {

JNIEXPORT void JNICALL Java_com_slark_sdk_SlarkLog_00024Companion_nativeLog(JNIEnv* env,
                                                                            jobject /*thiz*/,
                                                                            jstring message) {
    using namespace slark;
    auto logStr = JNI::FromJVM::str(env, message);
    outputLog(LogType::Record, "{}\n", logStr);
}
}
