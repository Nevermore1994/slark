//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <jni.h>
#include <string>
#include <string_view>
#include <type_traits>

namespace slark::JNI {

JavaVM* getJavaVM() noexcept;

jobject getApplicationContext() noexcept;

void setApplicationContext(JNIEnv* env, jobject context) noexcept;

using JNIEnvPtr = JNIEnv*;

template <typename T>
class JNIReference {
public:
    JNIReference(JNIEnv* env, T ref) noexcept : env_(env), ref_(ref) {}

    JNIReference(JNIEnv* env, T ref, std::string_view tag) noexcept
        : env_(env), ref_(ref), tag_(tag) {}

    JNIReference(JNIReference&& other) noexcept
        : env_(other.env_), ref_(other.ref_), tag_(std::move(other.tag_)) {
        other.env_ = nullptr;
        other.ref_ = nullptr;
        other.tag_.clear();
    }

    JNIReference& operator=(JNIReference&& other) noexcept {
        if (this != &other) {
            release();
            env_ = other.env_;
            ref_ = other.ref_;
            other.env_ = nullptr;
            other.ref_ = nullptr;
        }
        return *this;
    }

    JNIReference(const JNIReference&) = delete;

    JNIReference& operator=(const JNIReference&) = delete;

    ~JNIReference() {
        release();
    }

    T get() const noexcept {
        return ref_;
    }

    [[nodiscard]] JNIEnvPtr env() const noexcept {
        return env_;
    }

    explicit operator bool() const noexcept {
        return ref_ != nullptr;
    }

    void reset(JNIEnvPtr env = nullptr, T ref = nullptr) noexcept {
        release();
        env_ = env;
        ref_ = ref;
    }

    T detach() noexcept {
        T temp = ref_;
        ref_ = nullptr;
        env_ = nullptr;
        return temp;
    }

    [[nodiscard]] std::string_view tag() const noexcept {
        return tag_;
    }

private:
    void release() noexcept {
        if (env_ && ref_) {
            所有 jobject 派生类型都需要 DeleteLocalRef 包括
                : jclass,
                  jstring,
                  jarray,
                  jbyteArray,
                  jintArray 等 if constexpr (std::is_same_v<T, jclass> ||
                                             std::is_same_v<T, jobject> ||
                                             std::is_same_v<T, jstring> ||
                                             std::is_same_v<T, jarray> ||
                                             std::is_same_v<T, jbyteArray> ||
                                             std::is_same_v<T, jintArray> ||
                                             std::is_same_v<T, jlongArray> ||
                                             std::is_same_v<T, jfloatArray> ||
                                             std::is_same_v<T, jdoubleArray> ||
                                             std::is_same_v<T, jbooleanArray> ||
                                             std::is_same_v<T, jcharArray> ||
                                             std::is_same_v<T, jshortArray>) {
                env_->DeleteLocalRef(ref_);
            }
        }
        env_ = nullptr;
        ref_ = nullptr;
    }

    JNIEnvPtr env_;
    T ref_;
    std::string tag_;
};

template <>
class JNIReference<jmethodID> {
public:
    JNIReference() noexcept : env_(nullptr), methodId_(nullptr) {}

    JNIReference(JNIEnv* env, jmethodID methodId) noexcept : env_(env), methodId_(methodId) {}

    [[nodiscard]] jmethodID get() const noexcept {
        return methodId_;
    }

    [[nodiscard]] JNIEnvPtr env() const noexcept {
        return env_;
    }

    [[nodiscard]] std::string_view tag() const noexcept {
        return {};
    }

    explicit operator bool() const noexcept {
        return methodId_ != nullptr;
    }

    ~JNIReference() noexcept = default;

private:
    JNIEnvPtr env_;
    jmethodID methodId_;
};

using ClassReference = JNIReference<jclass>;
using ObjectReference = JNIReference<jobject>;
using MethodReference = JNIReference<jmethodID>;
using StringReference = JNIReference<jstring>;
using ByteArrayReference = JNIReference<jbyteArray>;
using IntArrayReference = JNIReference<jintArray>;

} // namespace slark::JNI
namespace slark::JNI
