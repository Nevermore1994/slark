//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "FileUtil.h"
#include "JNIBase.h"
#include "JNICache.h"
#include "JNICall.h"
#include "JNIEnvGuard.h"
#include "JNIMarshal.h"
#include "JNINames.h"

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
    static constexpr std::string_view kContextClass = JNI::JavaClass::AndroidContext;
    static constexpr std::string_view kFileClass = JNI::JavaClass::JavaFile;
    auto fileObj = JNI::JNICall::call<jobject>(env, context, kContextClass, methodName,
                                               JNI::ReturnSig(JNI::makeObject(kFileClass)));
    if (!fileObj) {
        return {};
    }

    // 优先使用 getCanonicalPath() 解析真实路径，避免 SELinux 拒绝符号链接访问
    // 失败时回退到 getPath()
    auto canonicalPath = JNI::JNICall::call<std::string>(env, fileObj.get(), kFileClass,
                                                         JNI::JavaMethod::GetCanonicalPath);
    if (!canonicalPath.empty()) {
        return canonicalPath;
    }
    auto path =
        JNI::JNICall::call<std::string>(env, fileObj.get(), kFileClass, JNI::JavaMethod::GetPath);
    return path;
}

}  // namespace

std::string rootPath() noexcept {
    static std::string path;
    static std::once_flag flag;
    std::call_once(flag, [] {
        path = getAppPath(JNI::JavaMethod::GetFilesDir);
    });
    if (path.empty()) {
        path = getAppPath(JNI::JavaMethod::GetFilesDir);
    }
    return path;
}

std::string cachePath() noexcept {
    static std::string path;
    static std::once_flag flag;
    std::call_once(flag, [] {
        path = getAppPath(JNI::JavaMethod::GetCacheDir);
    });
    if (path.empty()) {
        path = getAppPath(JNI::JavaMethod::GetCacheDir);
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

}  // namespace slark::File
