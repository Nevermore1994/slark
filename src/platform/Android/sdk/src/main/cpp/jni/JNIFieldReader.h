//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "JNIBase.h"
#include "JNISignature.h"

namespace slark::JNI {

template <typename T>
    requires(std::same_as<T, double> || std::same_as<T, int32_t> || std::same_as<T, int64_t>)
[[nodiscard]] bool readField(JNIEnv* env,
                             jobject object,
                             jclass cls,
                             std::string_view name,
                             T& value) {
    if (!env || !object || !cls || env->ExceptionCheck()) {
        return false;
    }
    constexpr auto signature = [] {
        if constexpr (std::same_as<T, double>) {
            return Sig::Double;
        } else if constexpr (std::same_as<T, int32_t>) {
            return Sig::Int;
        } else {
            return Sig::Long;
        }
    }();
    const auto fieldName = std::string(name);
    const auto fieldSignature = std::string(signature);
    const auto field = env->GetFieldID(cls, fieldName.c_str(), fieldSignature.c_str());
    if (!field) {
        return false;
    }
    if constexpr (std::same_as<T, double>) {
        value = env->GetDoubleField(object, field);
    } else if constexpr (std::same_as<T, int32_t>) {
        value = env->GetIntField(object, field);
    } else {
        value = env->GetLongField(object, field);
    }
    return !env->ExceptionCheck();
}

template <typename Group, typename Value>
[[nodiscard]] constexpr auto field(std::string_view name, Value Group::* member) noexcept {
    return std::pair{name, member};
}

template <typename Group, typename... Fields>
[[nodiscard]] bool readFields(JNIEnv* env, jobject object, Group& group, Fields... fields) {
    if (!env || !object || env->ExceptionCheck()) {
        return false;
    }
    ClassReference cls(env, env->GetObjectClass(object));
    return cls && (... && readField(env, object, cls.get(), fields.first, group.*(fields.second)));
}

[[nodiscard]] inline ObjectReference readObjectField(JNIEnv* env,
                                                     jobject object,
                                                     jclass cls,
                                                     std::string_view name,
                                                     std::string_view signature) {
    if (!env || !object || !cls || env->ExceptionCheck()) {
        return {};
    }
    const auto fieldName = std::string(name);
    const auto fieldSignature = std::string(signature);
    const auto id = env->GetFieldID(cls, fieldName.c_str(), fieldSignature.c_str());
    return {env, id ? env->GetObjectField(object, id) : nullptr};
}

struct GroupReadResult {
    std::string_view name;
    bool ok;
};

// Reads each group in order and stops at the first failure. The name of the failing group is
// returned so the caller can report which part of the object was missing or unreadable.
template <typename... Readers>
[[nodiscard]] GroupReadResult readGroups(Readers... readers) {
    GroupReadResult result;
    static_cast<void>((... && [&result](const Readers& reader) {
        result = reader();
        return result.ok;
    }(readers)));
    return result;
}

}  // namespace slark::JNI
