//
// Created by Nevermore on 2025/4/16.
//
#pragma once

#include <jni.h>
#include <type_traits>
#include <string_view>
#include <string>

namespace slark::JNI {

JavaVM* getJavaVM();

using JNIEnvPtr = JNIEnv*;

template<typename T>
class JNIReference {
public:
    JNIReference(JNIEnv* env, T ref) noexcept : env_(env), ref_(ref) {}

    JNIReference(JNIEnv* env, T ref, std::string_view tag) noexcept
        : env_(env)
        , ref_(ref)
        , tag_(tag) {

    }

    JNIReference(JNIReference&& other) noexcept
        : env_(other.env_)
        , ref_(other.ref_)
        , tag_(std::move(other.tag_)) {
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

    T get() const noexcept { return ref_; }

    JNIEnvPtr env() const noexcept { return env_; }

    explicit operator bool() const noexcept { return ref_ != nullptr; }

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

    std::string_view tag() const noexcept {
        return tag_;
    }
private:
    void release() noexcept {
        if (env_ && ref_) {
            if constexpr (std::is_same_v<T, jclass> ||
                        std::is_same_v<T, jobject> ||
                        std::is_same_v<T, jstring> ||
                        std::is_same_v<T, jarray>) {
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

template<>
class JNIReference<jmethodID> {
public:
    JNIReference() noexcept : methodId_(nullptr) {}
    JNIReference(JNIEnv* env, jmethodID methodId) noexcept
        : env_(env)
        , methodId_(methodId) {

    }

    jmethodID get() const noexcept { return methodId_; }
    explicit operator bool() const noexcept { return methodId_ != nullptr; }

    ~JNIReference() noexcept {
        if (env_ && env_->ExceptionCheck()) {
            env_->ExceptionDescribe();
            env_->ExceptionClear();
        }
        env_ = nullptr;
    }
private:
    JNIEnvPtr env_;
    jmethodID methodId_;
};

using ClassReference = JNIReference<jclass>;
using ObjectReference = JNIReference<jobject>;
using MethodReference = JNIReference<jmethodID>;
using StringReference = JNIReference<jstring>;

}