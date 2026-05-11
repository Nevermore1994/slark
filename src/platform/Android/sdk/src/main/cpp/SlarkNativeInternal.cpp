#include "SlarkNativeInternal.h"

#include "JNICall.h"
#include "JNIEnvGuard.h"
#include "JNINames.h"
#include "Log.hpp"

namespace slark {

void printLog(const std::string& logStr) {
    JNI::JNIEnvGuard guard(JNI::getJavaVM());
    if (!guard) {
        return;
    }
    JNI::JNICall::callStatic<void>(guard.get(),
                                   JNI::JavaClass::SlarkNativeBridge,
                                   JNI::JavaMethod::Logout,
                                   logStr);
}

} // namespace slark
