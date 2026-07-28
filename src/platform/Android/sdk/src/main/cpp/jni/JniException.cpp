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

    ObjectReference exRef(env, exception);

    auto className = [&]() -> std::string {
        auto classClass = JNICache::instance().getClass(env, "java/lang/Class");
        if (!classClass) {
            return "java/lang/Throwable";
        }
        auto getNameMethod =
            JNICache::instance().getMethod(classClass, "getName", "()Ljava/lang/String;");
        if (!getNameMethod) {
            return "java/lang/Throwable";
        }
        ClassReference exceptionClass(env, env->GetObjectClass(exception));
        if (!exceptionClass) {
            return "java/lang/Throwable";
        }
        StringReference nameStr(
            env,
            static_cast<jstring>(env->CallObjectMethod(exceptionClass.get(), getNameMethod.get())));
        return nameStr ? FromJVM::str(env, nameStr.get()) : "java/lang/Throwable";
    }();

    auto message = [&]() -> std::string {
        auto throwableClass = JNICache::instance().getClass(env, "java/lang/Throwable");
        if (!throwableClass) {
            return {};
        }
        auto getMessageMethod =
            JNICache::instance().getMethod(throwableClass, "getMessage", "()Ljava/lang/String;");
        if (!getMessageMethod) {
            return {};
        }
        StringReference msgStr(
            env,
            static_cast<jstring>(env->CallObjectMethod(exception, getMessageMethod.get())));
        return msgStr ? FromJVM::str(env, msgStr.get()) : std::string{};
    }();

    LogE("JniException: {}: {}", className, message);
    throw JniException(std::move(className), std::move(message));
}

}  // namespace slark::JNI
