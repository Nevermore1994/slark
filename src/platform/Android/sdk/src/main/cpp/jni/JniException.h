//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <jni.h>

#include <stdexcept>
#include <string>

namespace slark::JNI {

class JniException : public std::runtime_error {
 public:
    JniException(std::string javaClass, std::string message);

    const std::string& javaClassName() const noexcept;
    const std::string& javaMessage() const noexcept;

 private:
    std::string javaClass_;
    std::string javaMessage_;
};

void checkAndThrow(JNIEnv* env);

}  // namespace slark::JNI
