//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "JNIBase.h"

namespace slark::JNI {

class JNIEnvGuard {
public:
    explicit JNIEnvGuard(JavaVM* jvm) noexcept : jvm_(jvm), env_(nullptr), needDetach_(false) {
        if (!jvm_) {
            return;
        }

        int status = jvm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6);

        if (status == JNI_EDETACHED) {
            JavaVMAttachArgs args{.version = JNI_VERSION_1_6, .name = nullptr, .group = nullptr};
            status = jvm_->AttachCurrentThread(reinterpret_cast<JNIEnv**>(&env_), &args);
            if (status == JNI_OK) {
                needDetach_ = true;
            } else {
                env_ = nullptr;
            }
        } else if (status != JNI_OK) {
            env_ = nullptr;
        }
    }

    JNIEnvGuard(const JNIEnvGuard&) = delete;
    JNIEnvGuard& operator=(const JNIEnvGuard&) = delete;

    JNIEnvGuard(JNIEnvGuard&& other) noexcept
        : jvm_(other.jvm_), env_(other.env_), needDetach_(other.needDetach_) {
        other.jvm_ = nullptr;
        other.env_ = nullptr;
        other.needDetach_ = false;
    }

    JNIEnvGuard& operator=(JNIEnvGuard&& other) noexcept {
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

    [[nodiscard]] JNIEnv* get() const noexcept {
        return env_;
    }

    JNIEnv* operator->() const noexcept {
        return env_;
    }

    explicit operator bool() const noexcept {
        return env_ != nullptr;
    }

private:
    JavaVM* jvm_;
    JNIEnvPtr env_;
    bool needDetach_;
};

} // namespace slark::JNI
