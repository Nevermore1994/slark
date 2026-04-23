//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "FileUtil.h"
#include "JNIBase.h"
#include "JNICache.h"
#include "JNICall.h"
#include "JNIEnvGuard.h"
#include "JNIMarshal.h"

namespace slark::File {

namespace {

std::string getAppPath(const std::string& methodName) noexcept {
    auto jvm = JNI::getJavaVM();
    auto context = JNI::getApplicationContext();
    if (!jvm || !context) {
        return {};
    }

    JNI::JNIEnvGuard guard(jvm);
    auto* env = guard.get();
    if (!env) {
        return {};
    }

    // 调用 Context.methodName() -> java.io.File
    static constexpr std::string_view kContextClass = "android/content/Context";
    static constexpr std::string_view kFileClass = "java/io/File";
    auto fileObj = JNI::JNICall::call<jobject>(env,
                                               context,
                                               kContextClass,
                                               methodName,
                                               JNI::ReturnSig(JNI::makeObject(kFileClass)));
    if (!fileObj) {
        return {};
    }

    // 调用 File.getPath() -> String
    auto path = JNI::JNICall::call<std::string>(env, fileObj.get(), kFileClass, "getPath");
    return path.value_or(std::string{});
}

} // namespace

std::string rootPath() noexcept {
    static std::string path;
    static std::once_flag flag;
    std::call_once(flag, [] {
        path = getAppPath("getFilesDir");
    });
    if (path.empty()) {
        path = getAppPath("getFilesDir");
    }
    return path;
}

std::string cachePath() noexcept {
    static std::string path;
    static std::once_flag flag;
    std::call_once(flag, [] {
        path = getAppPath("getCacheDir");
    });
    if (path.empty()) {
        path = getAppPath("getCacheDir");
    }
    return path;
}

std::string tmpPath() noexcept {
    static std::string path;
    static std::once_flag flag;
    std::call_once(flag, [] {
        auto p = cachePath();
        if (!p.empty()) {
            p.append("/tmp");
            if (!isDirExist(p)) {
                createDir(p);
            }
            path = std::move(p);
        }
    });
    if (path.empty()) {
        auto p = cachePath();
        if (!p.empty()) {
            p.append("/tmp");
            if (!isDirExist(p)) {
                createDir(p);
            }
            path = p;
        }
    }
    return path;
}

} // namespace slark::File
