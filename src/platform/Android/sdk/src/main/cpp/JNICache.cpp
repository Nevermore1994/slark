//
// Created by Nevermore on 2025/3/27.
//

#include "JNICache.h"
#include "JNISignature.h"

namespace slark::JNI {

JNICache &JNICache::shareInstance() noexcept {
    static auto instance = std::unique_ptr<JNICache>(new JNICache());
    return *instance;
}

MethodReference JNICache::findMethodCache(
    const ClassReference &clazz,
    const std::string &key
) noexcept {
    std::shared_lock<std::shared_mutex> readLock(methodCacheMutex_);
    auto env = clazz.env();
    auto it = methodCache_.find(key);
    if (it == methodCache_.end()) {
        return {env, nullptr};
    }
    auto oldClass = ClassReference(env,jclass(env->NewLocalRef(it->second.classRef)));
    if (oldClass) {
        if (env->IsSameObject(
            oldClass.get(),
            clazz.get())) {
            return {
                env, it->second.methodId
            };
        }
    }
    return {env, nullptr};
}

MethodReference JNICache::updateMethodCache(
    const ClassReference &clazz,
    const std::string &key,
    jmethodID methodId
) noexcept {
    std::unique_lock<std::shared_mutex> writeLock(methodCacheMutex_);
    auto env = clazz.env();
    auto it = methodCache_.find(key);
    if (it != methodCache_.end()) {
        auto oldClass = ClassReference(
            env,
            jclass(env->NewLocalRef(it->second.classRef)));
        if (oldClass) {
            if (env->IsSameObject(
                oldClass.get(),
                clazz.get())) {
                return {
                    env, it->second
                        .methodId
                };
            }
        }
        env->DeleteWeakGlobalRef(
            it->second
                .classRef
        );
        methodCache_.erase(it);
    }

    if (methodId != nullptr) {
        methodCache_[key] = {
            methodId,
            env->NewWeakGlobalRef(clazz.get())
        };
    }
    return {env, methodId};
}

MethodReference JNICache::getStaticMethodId(
    const ClassReference &clazz,
    std::string_view methodName,
    std::string_view signature
) noexcept {
    std::string key = std::string(clazz.tag()).append(methodName)
        .append(signature);
    auto env = clazz.env();
    if (!env) {
        return {};
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    if (auto cached = findMethodCache(clazz, key)) {
        return {cached};
    }
    auto methodId = env->GetStaticMethodID(
        clazz.get(),
        methodName.data(),
        signature.data());
    if (methodId == nullptr) {
        return MethodReference{env, nullptr};
    }
    updateMethodCache(
        clazz,
        key,
        methodId
    );
    return MethodReference{env, methodId};
}

MethodReference JNICache::getMethodId(
    const ClassReference &clazz,
    std::string_view methodName,
    std::string_view signature
) noexcept {
    std::string key = std::string(clazz.tag()).append(methodName)
        .append(signature);
    auto env = clazz.env();
    if (!env) {
        return {};
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    if (auto cached = findMethodCache(clazz, key)) {
        return {cached};
    }
    auto methodId = env->GetMethodID(
        clazz.get(),
        methodName.data(),
        signature.data());
    if (methodId == nullptr) {
        return {env, nullptr};
    }
    updateMethodCache(
        clazz,
        key,
        methodId
    );
    return {env, methodId};
}

ClassReference JNICache::getClass(
    JNIEnv *env,
    std::string_view className
) noexcept {
    if (!env) {
        return {env, nullptr};
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    std::string key(className);
    {
        std::shared_lock<std::shared_mutex> readLock(classCacheMutex_);
        auto it = classCache_.find(key);
        if (it != classCache_.end()) {
            auto cls = (jclass) env->NewLocalRef(
                it->second
                    .classRef
            );
            if (cls != nullptr) {
                it->second
                    .lastAccessTime = Time::nowTimeStamp();
                return {env, cls, className};
            }
        }
    }

    std::unique_lock<std::shared_mutex> writeLock(classCacheMutex_);
    auto it = classCache_.find(key);
    if (it != classCache_.end()) {
        auto cls = (jclass) env->NewLocalRef(
            it->second
                .classRef
        );
        if (cls != nullptr) {
            it->second
                .lastAccessTime = Time::nowTimeStamp();
            return {env, cls, className};
        }
        env->DeleteWeakGlobalRef(
            it->second
                .classRef
        );
        classCache_.erase(it);
    }

    jclass cls = env->FindClass(className.data());
    if (cls != nullptr) {
        jweak weakRef = env->NewWeakGlobalRef(cls);
        classCache_[key] = {
            weakRef,
            Time::nowTimeStamp()
        };
    }

    return {env, cls, className};
}

ObjectReference JNICache::getEnumField(
    JNIEnv *env,
    std::string_view className,
    std::string_view fieldName
) noexcept {
    if (!env) {
        return {env, nullptr};
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    auto enumClass = getClass(env, className);
    if (!enumClass) {
        return {env, nullptr, fieldName};
    }

    jfieldID fieldId = env->GetStaticFieldID(
        enumClass.get(),
        fieldName.data(),
        JNI::makeObject(className).data());
    if (fieldId == nullptr) {
        return {env, nullptr, fieldName};
    }

    jobject field = env->GetStaticObjectField(
        enumClass.get(),
        fieldId
    );
    return {env, field, fieldName};
}

} // slark