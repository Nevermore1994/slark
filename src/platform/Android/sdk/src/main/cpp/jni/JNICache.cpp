//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "JNICache.h"

#include "JNISignature.h"

namespace slark::JNI {

namespace {

// 清理 JNI 异常状态
void clearException(JNIEnv* env) noexcept {
    if (env && env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

// 构建方法缓存 key
std::string makeMethodKey(std::string_view classTag,
                          std::string_view methodName,
                          std::string_view signature) {
    std::string key;
    key.reserve(classTag.size() + methodName.size() + signature.size());
    key.append(classTag).append(methodName).append(signature);
    return key;
}

} // namespace

ClassReference JNICache::getClass(JNIEnv* env, std::string_view className) noexcept {
    if (!env) {
        return {env, nullptr};
    }

    clearException(env);

    std::string key(className);

    // 先用读锁查找
    {
        std::shared_lock lock(classCacheMutex_);
        if (auto it = classCache_.find(key); it != classCache_.end()) {
            if (auto cls = static_cast<jclass>(env->NewLocalRef(it->second.classRef))) {
                return {env, cls, className};
            }
        }
    }

    // 写锁：检查并更新
    {
        std::unique_lock lock(classCacheMutex_);
        if (auto it = classCache_.find(key); it != classCache_.end()) {
            if (auto cls = static_cast<jclass>(env->NewLocalRef(it->second.classRef))) {
                return {env, cls, className};
            }
            // 弱引用已失效，清理旧条目
            env->DeleteWeakGlobalRef(it->second.classRef);
            classCache_.erase(it);
        }

        // 查找类并缓存
        if (auto cls = env->FindClass(className.data())) {
            auto weakRef = env->NewWeakGlobalRef(cls);
            classCache_.emplace(std::move(key), ClassInfo{weakRef});
            return {env, cls, className};
        }
    }

    return {env, nullptr};
}

MethodReference JNICache::findMethod(const ClassReference& clazz, const std::string& key) noexcept {
    auto env = clazz.env();
    std::shared_lock lock(methodCacheMutex_);

    if (auto it = methodCache_.find(key); it != methodCache_.end()) {
        ClassReference oldClass(env, static_cast<jclass>(env->NewLocalRef(it->second.classRef)));
        if (oldClass && env->IsSameObject(oldClass.get(), clazz.get())) {
            return {env, it->second.methodId};
        }
    }
    return {env, nullptr};
}

MethodReference JNICache::updateMethod(const ClassReference& clazz,
                                       const std::string& key,
                                       jmethodID methodId) noexcept {
    auto env = clazz.env();
    std::unique_lock lock(methodCacheMutex_);

    // 检查是否已存在且有效
    if (auto it = methodCache_.find(key); it != methodCache_.end()) {
        ClassReference oldClass(env, static_cast<jclass>(env->NewLocalRef(it->second.classRef)));
        if (oldClass && env->IsSameObject(oldClass.get(), clazz.get())) {
            return {env, it->second.methodId};
        }
        // 清理失效条目
        env->DeleteWeakGlobalRef(it->second.classRef);
        methodCache_.erase(it);
    }

    // 添加新条目
    if (methodId) {
        methodCache_.emplace(key, MethodInfo{methodId, env->NewWeakGlobalRef(clazz.get())});
    }
    return {env, methodId};
}

MethodReference JNICache::getStaticMethod(const ClassReference& clazz,
                                          std::string_view methodName,
                                          std::string_view signature) noexcept {
    auto env = clazz.env();
    if (!env) {
        return {};
    }

    clearException(env);

    auto key = makeMethodKey(clazz.tag(), methodName, signature);
    if (auto cached = findMethod(clazz, key)) {
        return cached;
    }

    auto methodId = env->GetStaticMethodID(clazz.get(), methodName.data(), signature.data());
    return methodId ? updateMethod(clazz, key, methodId) : MethodReference{env, nullptr};
}

MethodReference JNICache::getMethod(const ClassReference& clazz,
                                    std::string_view methodName,
                                    std::string_view signature) noexcept {
    auto env = clazz.env();
    if (!env) {
        return {};
    }

    clearException(env);

    auto key = makeMethodKey(clazz.tag(), methodName, signature);
    if (auto cached = findMethod(clazz, key)) {
        return cached;
    }

    auto methodId = env->GetMethodID(clazz.get(), methodName.data(), signature.data());
    return methodId ? updateMethod(clazz, key, methodId) : MethodReference{env, nullptr};
}

ObjectReference JNICache::getEnumField(JNIEnv* env,
                                       std::string_view className,
                                       std::string_view fieldName) noexcept {
    if (!env) {
        return {env, nullptr};
    }

    clearException(env);

    std::string key;
    key.reserve(className.size() + fieldName.size() + 1);
    key.append(className).append(1, '.').append(fieldName);

    // 先用读锁查找缓存
    {
        std::shared_lock lock(enumFieldCacheMutex_);
        if (auto it = enumFieldCache_.find(key); it != enumFieldCache_.end()) {
            if (auto local = env->NewLocalRef(it->second.fieldRef)) {
                return {env, local, fieldName};
            }
        }
    }

    // 未命中，查找并写入缓存
    auto enumClass = getClass(env, className);
    if (!enumClass) {
        return {env, nullptr, fieldName};
    }

    auto fieldId =
        env->GetStaticFieldID(enumClass.get(), fieldName.data(), makeObject(className).c_str());
    if (!fieldId) {
        return {env, nullptr, fieldName};
    }

    auto field = env->GetStaticObjectField(enumClass.get(), fieldId);
    if (!field) {
        return {env, nullptr, fieldName};
    }

    // 写入缓存（weak global ref）
    {
        std::unique_lock lock(enumFieldCacheMutex_);
        if (auto it = enumFieldCache_.find(key); it == enumFieldCache_.end()) {
            enumFieldCache_.emplace(key, EnumFieldInfo{env->NewWeakGlobalRef(field)});
        }
    }

    return {env, field, fieldName};
}

void JNICache::clear() {
    auto vm = getJavaVM();
    if (!vm) {
        return;
    }

    JNIEnv* env = nullptr;

    // JNI_OnUnload 在已 attach 的线程中调用，无需临时 attach / detach
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env) {
        return;
    }

    {
        std::unique_lock lock(classCacheMutex_);
        for (auto& [_, info] : classCache_) {
            env->DeleteWeakGlobalRef(info.classRef);
        }
        classCache_.clear();
    }
    {
        std::unique_lock lock(methodCacheMutex_);
        for (auto& [_, info] : methodCache_) {
            env->DeleteWeakGlobalRef(info.classRef);
        }
        methodCache_.clear();
    }
    {
        std::unique_lock lock(enumFieldCacheMutex_);
        for (auto& [_, info] : enumFieldCache_) {
            env->DeleteWeakGlobalRef(info.fieldRef);
        }
        enumFieldCache_.clear();
    }
}

} // namespace slark::JNI
