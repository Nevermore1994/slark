#include <string>
#include <mutex>
#include <vector>
#include <algorithm>
#include "Log.hpp"
#include "JNIEnvGuard.hpp"
#include "JNICache.h"
#include "JNIHelper.h"
#include "FileUtil.h"

JavaVM *gJavaVM = nullptr;
jobject gApplicationContext = nullptr;

using namespace slark;

void loadClassCache(JNIEnv *env) {
    using namespace slark::JNI;
    std::vector<std::string> classes = {
        "com/slark/sdk/SlarkNativeBridge",
        "com/slark/sdk/SlarkPlayerManager",
        "com/slark/api/SlarkPlayerState",
        "com/slark/api/SlarkPlayerEvent",
        "com/slark/sdk/MediaCodecDecoder$Action",
        "com/slark/sdk/MediaCodecDecoder",
        "com/slark/sdk/AudioPlayer",
        "com/slark/sdk/AudioPlayer$Action",
        "com/slark/sdk/AudioPlayer$Config",
        "com/slark/sdk/RenderTexture",
    };
    std::for_each(
        classes.begin(),
        classes.end(),
        [&](const std::string &className) {
            auto classRef = JNICache::shareInstance().getClass(
                env,
                className
            );
            if (!classRef) {
                LogE("Failed to find class: {}",
                     className);
            }
        }
    );
}

JNIEXPORT jint JNICALL JNI_OnLoad(
    JavaVM *vm,
    void * /* reserved */) {
    gJavaVM = vm;
    JNIEnv *env;
    if (vm->GetEnv(
        reinterpret_cast<void **>(&env),
        JNI_VERSION_1_6
    ) != JNI_OK) {
        return -1;
    }
    loadClassCache(env);
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(
    JavaVM *vm,
    void * /* reserved */) {
    JNIEnv *env;
    vm->GetEnv(
        reinterpret_cast<void **>(&env),
        JNI_VERSION_1_6
    );
    if (gApplicationContext) {
        env->DeleteGlobalRef(gApplicationContext);
        gApplicationContext = nullptr;
    }
    gJavaVM = nullptr;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkNativeBridge_logout(
    JNIEnv *env,
    jobject /* this*/,
    const std::string &log
) {
    using namespace slark::JNI;
    auto bridgeClass = JNICache::shareInstance().getClass(
        env,
        "com/slark/sdk/SlarkNativeBridge"
    );
    if (!bridgeClass) {
        LogE("Failed to find SlarkNativeBridge class");
        return;
    }
    auto methodId = JNICache::shareInstance().getStaticMethodId(
        bridgeClass,
        "logout",
        "(Ljava/lang/String;)V"
    );
    if (!methodId) {
        LogE("Failed to find logout method in SlarkNativeBridge class");
        return;
    }
    auto message = ToJVM::toString(
        env,
        log
    );
    if (!message) {
        LogE("Failed to convert log message to jstring");
        return;
    }
    env->CallStaticVoidMethod(
        bridgeClass.get(),
        methodId.get(),
        message.get());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_slark_sdk_SlarkNativeBridge_00024Companion_setContext(
    JNIEnv *env,
    jobject /*thiz*/,
    jobject context
) {
    if (gApplicationContext) {
        env->DeleteGlobalRef(gApplicationContext);
    }
    gApplicationContext = env->NewGlobalRef(context);
}

bool getAppPath(
    JNIEnv *env,
    jobject context,
    const std::string &methodName,
    std::string &path
) {
    jclass contextClass = env->GetObjectClass(context);
    if (contextClass == nullptr) {
        return false;
    }

    jmethodID getFilesDirMethod = env->GetMethodID(
        contextClass,
        methodName.c_str(),
        "()Ljava/io/File;"
    );
    if (getFilesDirMethod == nullptr) {
        return false;
    }
    jobject fileObject = env->CallObjectMethod(
        context,
        getFilesDirMethod
    );
    if (fileObject == nullptr) {
        return false;
    }

    jclass fileClass = env->GetObjectClass(fileObject);
    if (fileClass == nullptr) {
        return false;
    }
    jmethodID getPathMethod = env->GetMethodID(
        fileClass,
        "getPath",
        "()Ljava/lang/String;"
    );
    if (getPathMethod == nullptr) {
        return false;
    }

    auto pathString = reinterpret_cast<jstring>(env->CallObjectMethod(
        fileObject,
        getPathMethod
    ));
    const char *pathCStr = env->GetStringUTFChars(
        pathString,
        nullptr
    );
    path = std::string(pathCStr);
    env->ReleaseStringUTFChars(
        pathString,
        pathCStr
    );

    return true;
}

namespace slark {

namespace JNI {

JavaVM *getJavaVM() {
    return gJavaVM;
}

}
using namespace JNI;

void printLog(const std::string &logStr) {
    JNIEnvGuard guard(gJavaVM);
    Java_com_slark_sdk_SlarkNativeBridge_logout(
        guard.get(),
        nullptr,
        logStr
    );
}

namespace File {

std::string rootPath() noexcept {
    static auto rootPath = []() {
        std::string rootPath;
        JNIEnvGuard guard(gJavaVM);
        getAppPath(
            guard.get(),
            gApplicationContext,
            "getFilesDir",
            rootPath
        );
        return rootPath;
    }();
    return rootPath;
}

std::string cachePath() noexcept {
    static auto cachePath = []() {
        std::string path;
        JNIEnvGuard guard(gJavaVM);
        getAppPath(
            guard.get(),
            gApplicationContext,
            "getCacheDir",
            path
        );
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