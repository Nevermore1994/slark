//
// Created by Nevermore on 2025/3/8.
//

#pragma once

#include "JNIBase.hpp"
#include <stdexcept>

namespace slark::JNI {

class JNIEnvGuard {
public:
    explicit JNIEnvGuard(JavaVM *jvm)
        : jvm_(jvm),
          env_(nullptr),
          needDetach_(false) {
        if (!jvm_) {
            throw std::runtime_error("nullptr JavaVM pointer");
        }

        int status = jvm_->GetEnv(
            reinterpret_cast<void **>(&env_),
            JNI_VERSION_1_6
        );

        if (status == JNI_EDETACHED) {
            JavaVMAttachArgs args{
                .version = JNI_VERSION_1_6,
                .name = nullptr,
                .group = nullptr
            };

            status = jvm_->AttachCurrentThread(
                reinterpret_cast<JNIEnv **>(&env_),
                &args
            );
            if (status == JNI_OK) {
                needDetach_ = true;
            } else {
                throw std::runtime_error("Failed to attach current thread to JVM");
            }
        } else if (status != JNI_OK) {
            throw std::runtime_error("Failed to get JNIEnv");
        }
    }

    JNIEnvGuard(const JNIEnvGuard &) = delete;

    JNIEnvGuard &operator=(const JNIEnvGuard &) = delete;

    JNIEnvGuard(JNIEnvGuard &&other) noexcept
        : jvm_(other.jvm_),
          env_(other.env_),
          needDetach_(other.needDetach_) {
        other.jvm_ = nullptr;
        other.env_ = nullptr;
        other.needDetach_ = false;
    }

    JNIEnvGuard &operator=(JNIEnvGuard &&other) noexcept {
        if (this != &other) {
            if (needDetach_ && jvm_) {
                jvm_->DetachCurrentThread();
            }

            jvm_ = other.jvm_;
            env_ = other.env_;
            needDetach_ = other.needDetach_;

            other.jvm_ = nullptr;
            other.env_ = nullptr;
            other.needDetach_ = false;
        }
        return *this;
    }

    ~JNIEnvGuard() {
        if (needDetach_ && jvm_) {
            jvm_->DetachCurrentThread();
        }
    }

    [[nodiscard]] JNIEnv *get() const noexcept {
        return env_;
    }

    JNIEnv *operator->() const noexcept {
        return env_;
    }

    explicit operator bool() const noexcept {
        return env_ != nullptr;
    }

private:
    JavaVM *jvm_;
    JNIEnvPtr env_;
    bool needDetach_;
};

} //namespace slark
