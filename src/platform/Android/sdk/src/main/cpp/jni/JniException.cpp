//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "JniException.h"

#include "JNICache.h"
#include "JNIMarshal.h"
#include "Log.hpp"

namespace slark::JNI {

JniException::JniException(std::string javaClass, std::string message)
    : std::runtime_error("JNI exception: " + javaClass + ": " + message),
      javaClass_(std::move(javaClass)),
      javaMessage_(std::move(message)) {}

const std::string& JniException::javaClassName() const noexcept {
    return javaClass_;
}

const std::string& JniException::javaMessage() const noexcept {
    return javaMessage_;
}

void checkAndThrow(JNIEnv* env) {
    if (!env || !env->ExceptionCheck()) {
        return;
    }

    jthrowable exception = env->ExceptionOccurred();
    if (!exception) {
        env->ExceptionClear();
        throw JniException("java/lang/Throwable",
                           "ExceptionCheck returned true but ExceptionOccurred returned null");
    }

    // 必须先 clear，才能安全调用其他 JNI 方法（CheckJNI 在 pending exception 下会 abort）
    env->ExceptionClear();

    // RAII 管理 exception local ref
    ObjectReference exRef(env, exception);

    std::string className = "java/lang/Throwable";
    std::string message;

    // 通过 Class.getName() 获取异常类名
    auto classClass = JNICache::instance().getClass(env, "java/lang/Class");
    if (classClass) {
        auto getNameMethod =
            JNICache::instance().getMethod(classClass, "getName", "()Ljava/lang/String;");
        if (getNameMethod) {
            ClassReference exceptionClass(env, env->GetObjectClass(exception));
            if (exceptionClass) {
                StringReference nameStr(
                    env,
                    static_cast<jstring>(
                        env->CallObjectMethod(exceptionClass.get(), getNameMethod.get())));
                if (nameStr) {
                    className = FromJVM::str(env, nameStr.get());
                }
            }
        }
    }

    // 通过 Throwable.getMessage() 获取异常消息
    auto throwableClass = JNICache::instance().getClass(env, "java/lang/Throwable");
    if (throwableClass) {
        auto getMessageMethod =
            JNICache::instance().getMethod(throwableClass, "getMessage", "()Ljava/lang/String;");
        if (getMessageMethod) {
            StringReference msgStr(
                env,
                static_cast<jstring>(env->CallObjectMethod(exception, getMessageMethod.get())));
            if (msgStr) {
                message = FromJVM::str(env, msgStr.get());
            }
        }
    }

    LogE("JniException: %s: %s", className.c_str(), message.c_str());
    throw JniException(std::move(className), std::move(message));
}

} // namespace slark::JNI
