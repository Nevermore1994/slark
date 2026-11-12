//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "JNIBase.h"
#include "JNICache.h"
#include "JNIMarshal.h"
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

[[nodiscard]] inline bool readStringList(JNIEnv* env,
                                         jobject object,
                                         jclass cls,
                                         std::string_view name,
                                         std::vector<std::string>& out) {
    const auto list = readObjectField(env, object, cls, name, Sig::List);
    if (!env || env->ExceptionCheck()) {
        return false;
    }
    if (!list) {
        return true;
    }
    ClassReference listClass(env, env->GetObjectClass(list.get()));
    const auto size = listClass ? JNICache::instance().getMethod(listClass, "size", "()I")
                                : MethodReference{};
    const auto get = listClass
                         ? JNICache::instance().getMethod(listClass, "get", "(I)Ljava/lang/Object;")
                         : MethodReference{};
    if (!size || !get) {
        return false;
    }
    const auto count = env->CallIntMethod(list.get(), size.get());
    if (env->ExceptionCheck()) {
        return false;
    }
    out.clear();
    out.reserve(static_cast<size_t>(count));
    for (jint i = 0; i < count; ++i) {
        ObjectReference item(env, env->CallObjectMethod(list.get(), get.get(), i));
        if (env->ExceptionCheck()) {
            return false;
        }
        out.push_back(item ? FromJVM::str(env, static_cast<jstring>(item.get())) : std::string{});
    }
    return true;
}

// Error value naming the group that was missing or unreadable.
using GroupReadError = std::string_view;
using GroupRead = std::expected<void, GroupReadError>;

// Reads each group in order and stops at the first failure.
template <typename... Readers>
[[nodiscard]] GroupRead readGroups(Readers... readers) {
    std::optional<GroupReadError> failure;
    static_cast<void>((... && [&failure](const Readers& reader) {
        const auto result = reader();
        if (!result.has_value()) {
            failure = result.error();
        }
        return result.has_value();
    }(readers)));
    return failure ? GroupRead{std::unexpected(*failure)} : GroupRead{};
}

}  // namespace slark::JNI
