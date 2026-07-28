//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "JNIBase.h"

namespace slark::JNI {

class JNICache {
 public:
    static JNICache& instance() noexcept {
        static JNICache cache;
        return cache;
    }

    ClassReference getClass(JNIEnv* env, std::string_view className) noexcept;

    MethodReference getStaticMethod(const ClassReference& clazz,
                                    std::string_view methodName,
                                    std::string_view signature) noexcept;

    MethodReference getMethod(const ClassReference& clazz,
                              std::string_view methodName,
                              std::string_view signature) noexcept;

    ObjectReference getEnumField(JNIEnv* env,
                                 std::string_view className,
                                 std::string_view fieldName) noexcept;

    void clear();

 private:
    JNICache() = default;

    ~JNICache() {
        clear();
    }

    MethodReference findMethod(const ClassReference& clazz, const std::string& key) noexcept;
    MethodReference updateMethod(const ClassReference& clazz,
                                 const std::string& key,
                                 jmethodID methodId) noexcept;

    struct MethodInfo {
        jmethodID methodId = nullptr;
        jweak classRef = nullptr;
    };

    struct ClassInfo {
        jweak classRef = nullptr;
    };

    struct EnumFieldInfo {
        jweak fieldRef = nullptr;
    };

    std::unordered_map<std::string, ClassInfo> classCache_;
    mutable std::shared_mutex classCacheMutex_;

    std::unordered_map<std::string, MethodInfo> methodCache_;
    mutable std::shared_mutex methodCacheMutex_;

    std::unordered_map<std::string, EnumFieldInfo> enumFieldCache_;
    mutable std::shared_mutex enumFieldCacheMutex_;
};

}  // namespace slark::JNI
