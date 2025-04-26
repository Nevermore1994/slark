#include <string>
#include <mutex>
#include "SlarkNative.h"
#include "AndroidBase.h"
#include "FileUtil.h"

JavaVM* gJavaVM = nullptr;
jobject gApplicationContext = nullptr;

using namespace slark;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /* reserved */) {
    gJavaVM = vm;
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* /* reserved */) {
    JNIEnv* env;
    vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (gApplicationContext) {
        env->DeleteGlobalRef(gApplicationContext);
        gApplicationContext = nullptr;
    }
    gJavaVM = nullptr;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkNativeBridge_logout(JNIEnv *env, jobject /* this*/, const std::string& log) {
    jclass nativeBridgeClass = env->FindClass("com/slark/sdk/SlarkNativeBridge");
    if (!nativeBridgeClass) return;

    jmethodID methodId = env->GetStaticMethodID(nativeBridgeClass, "logout", "(Ljava/lang/String;)V");
    if (!methodId) return;
    jstring message = env->NewStringUTF(log.c_str());

    env->CallStaticVoidMethod(nativeBridgeClass, methodId, message);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkNativeBridge_00024Companion_setContext(JNIEnv *env, jobject thiz,
                                                               jobject context) {
    if (gApplicationContext) {
        env->DeleteGlobalRef(gApplicationContext);
    }
    gApplicationContext = env->NewGlobalRef(context);
}

bool getAppPath(JNIEnv* env, jobject context, const std::string& methodName, std::string& path) {
    jclass contextClass = env->GetObjectClass(context);
    if (contextClass == nullptr) {
        return false;
    }

    jmethodID getFilesDirMethod = env->GetMethodID(contextClass, methodName.c_str(), "()Ljava/io/File;");
    if (getFilesDirMethod == nullptr) {
        return false;
    }
    jobject fileObject = env->CallObjectMethod(context, getFilesDirMethod);
    if (fileObject == nullptr) {
        return false;
    }

    jclass fileClass = env->GetObjectClass(fileObject);
    if (fileClass == nullptr) {
        return false;
    }
    jmethodID getPathMethod = env->GetMethodID(fileClass, "getPath", "()Ljava/lang/String;");
    if (getPathMethod == nullptr) {
        return false;
    }

    auto pathString = reinterpret_cast<jstring>(env->CallObjectMethod(fileObject, getPathMethod));
    const char* pathCStr = env->GetStringUTFChars(pathString, nullptr);
    path = std::string(pathCStr);
    env->ReleaseStringUTFChars(pathString, pathCStr);

    return true;
}

namespace slark {

using namespace slark::JNI;

JavaVM* getJavaVM() {
    return gJavaVM;
}

void printLog(const std::string& logStr) {
    JNIEnvGuard guard(gJavaVM);
    Java_com_slark_sdk_SlarkNativeBridge_logout(guard.get(), nullptr, logStr);
}

namespace FileUtil {

std::string rootPath() noexcept {
    static auto rootPath = [](){
        std::string rootPath;
        JNIEnvGuard guard(gJavaVM);
        getAppPath(guard.get(), gApplicationContext, "getFilesDir", rootPath);
        return rootPath;
    }();
    return rootPath;
}

std::string cachePath() noexcept {
    static auto cachePath = []() {
        std::string path;
        JNIEnvGuard guard(gJavaVM);
        getAppPath(guard.get(), gApplicationContext, "getCacheDir", path);
        return path;
    }();
    return cachePath;
}

std::string tmpPath() noexcept {
    static auto path = []() {
        auto path = cachePath();
        if (path.empty()) {
            return std::string();
        }
        path.append("/tmp");
        if (!isDirExist(path)) {
            createDir(path);
        }
        return path;
    }();
    return path;
}

}//FileUtil

}