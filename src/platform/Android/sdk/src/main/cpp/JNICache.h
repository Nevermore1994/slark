//
// Created by Nevermore on 2025/3/27.
//

#pragma once

#include "JNIBase.hpp"
#include "Time.hpp"
#include <unordered_map>
#include <shared_mutex>

namespace slark::JNI {

class JNICache {
public:
    static JNICache &shareInstance() noexcept;

    ~JNICache() = default;

    MethodReference getStaticMethodId(
        const ClassReference &clazz,
        std::string_view methodName,
        std::string_view signature
    ) noexcept;

    MethodReference getMethodId(
        const ClassReference &clazz,
        std::string_view methodName,
        std::string_view signature
    ) noexcept;

    ClassReference getClass(
        JNIEnv *env,
        std::string_view className
    ) noexcept;

    ObjectReference getEnumField(
        JNIEnv *env,
        std::string_view className,
        std::string_view fieldName
    ) noexcept;

private:
    JNICache() = default;

    MethodReference findMethodCache(
        const ClassReference &clazz,
        const std::string &key
    ) noexcept;

    MethodReference updateMethodCache(
        const ClassReference &clazz,
        const std::string &key,
        jmethodID methodId
    ) noexcept;

private:
    struct ClassInfo {
        jweak classRef = nullptr;
        Time::TimePoint lastAccessTime{0};
    };

    std::unordered_map<std::string, ClassInfo> classCache_;
    std::shared_mutex classCacheMutex_;

    struct MethodInfo {
        jmethodID methodId = nullptr;
        jweak classRef = nullptr;
    };

    std::unordered_map<std::string, MethodInfo> methodCache_;
    std::shared_mutex methodCacheMutex_;
};

} // slark

