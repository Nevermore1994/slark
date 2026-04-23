//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "Data.h"
#include "JNIBase.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace slark::JNI {

namespace FromJVM {

std::string str(JNIEnv* env, jstring s) noexcept;

std::unique_ptr<slark::Data> bytes(JNIEnv* env, jbyteArray arr) noexcept;

std::unique_ptr<slark::Data> bytes(JNIEnv* env, jobject byteBuffer) noexcept;

std::vector<int32_t> ints(JNIEnv* env, jintArray arr) noexcept;

} // namespace FromJVM

namespace ToJVM {

StringReference str(JNIEnv* env, std::string_view s) noexcept;

ByteArrayReference bytes(JNIEnv* env, const slark::Data& data) noexcept;

jobject byteBuffer(JNIEnv* env, const slark::Data& data) noexcept;

IntArrayReference ints(JNIEnv* env, const std::vector<int32_t>& data) noexcept;

ByteArrayReference naluBytes(JNIEnv* env,
                             const slark::Data& data,
                             bool skipExistingHeader = true) noexcept;

} // namespace ToJVM

} // namespace slark::JNI
