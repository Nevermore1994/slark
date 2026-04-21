//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "JNIMarshal.h"

namespace slark::JNI {

namespace FromJVM {

std::string str(JNIEnv* env, jstring s) noexcept {
    if (!env || !s) {
        return {};
    }
    const char* cStr = env->GetStringUTFChars(s, nullptr);
    if (!cStr) {
        return {};
    }
    std::string result(cStr);
    env->ReleaseStringUTFChars(s, cStr);
    return result;
}

std::unique_ptr<slark::Data> bytes(JNIEnv* env, jbyteArray arr) noexcept {
    if (!env || !arr) {
        return nullptr;
    }
    const jsize len = env->GetArrayLength(arr);
    auto buffer = std::make_unique<slark::Data>(static_cast<uint64_t>(len));
    env->GetByteArrayRegion(arr, 0, len, reinterpret_cast<jbyte*>(buffer->data()));
    return buffer;
}

std::vector<int32_t> ints(JNIEnv* env, jintArray arr) noexcept {
    if (!env || !arr) {
        return {};
    }
    const jsize len = env->GetArrayLength(arr);
    std::vector<int32_t> result(len);
    env->GetIntArrayRegion(arr, 0, len, result.data());
    return result;
}

std::unique_ptr<slark::Data> bytes(JNIEnv* env, jobject byteBuffer) noexcept {
    if (!env || !byteBuffer) {
        return nullptr;
    }
    auto* ptr = env->GetDirectBufferAddress(byteBuffer);
    const auto kCapacity = env->GetDirectBufferCapacity(byteBuffer);
    if (!ptr || kCapacity <= 0) {
        return nullptr;
    }
    return std::make_unique<slark::Data>(reinterpret_cast<const Byte*>(ptr),
                                         static_cast<uint64_t>(kCapacity));
}

} // namespace FromJVM

namespace FromJVM

    namespace ToJVM {

StringReference str(JNIEnv* env, std::string_view s) noexcept {
    if (!env) {
        return {env, nullptr};
    }
    return {env, env->NewStringUTF(s.empty() ? "" : s.data())};
}

ByteArrayReference bytes(JNIEnv* env, const slark::Data& data) noexcept {
    if (!env) {
        return {env, nullptr};
    }
    jbyteArray arr = env->NewByteArray(static_cast<jsize>(data.size()));
    if (arr && !data.empty()) {
        env->SetByteArrayRegion(arr,
                                0,
                                static_cast<jsize>(data.size()),
                                reinterpret_cast<const jbyte*>(data.data()));
    }
    return {env, arr};
}

jobject byteBuffer(JNIEnv* env, const slark::Data& data) noexcept {
    if (!env || data.empty()) {
        return nullptr;
    }
    return env->NewDirectByteBuffer(const_cast<void*>(static_cast<const void*>(data.data())),
                                    static_cast<jlong>(data.size()));
}

IntArrayReference ints(JNIEnv* env, const std::vector<int32_t>& data) noexcept {
    if (!env) {
        return {env, nullptr};
    }
    jintArray arr = env->NewIntArray(static_cast<jsize>(data.size()));
    if (arr && !data.empty()) {
        env->SetIntArrayRegion(arr, 0, static_cast<jsize>(data.size()), data.data());
    }
    return {env, arr};
}

ByteArrayReference naluBytes(JNIEnv* env,
                             const slark::Data& data,
                             bool skipExistingHeader) noexcept {
    if (!env) {
        return {env, nullptr};
    }

    计算跳过已有header后的偏移和大小 const uint32_t kHeaderSize = 4;
    uint32_t offset = 0;
    uint32_t size = static_cast<uint32_t>(data.size());

    if (skipExistingHeader && size > kHeaderSize) {
        offset = kHeaderSize;
        size -= kHeaderSize;
    }

    创建包含起始码的NALU数据
    jbyteArray arr = env->NewByteArray(static_cast<jsize>(size + kHeaderSize));
    if (!arr) {
        return {env, nullptr};
    }

    NALU起始码
    constexpr jbyte kNaluHeader[4] = {0x00, 0x00, 0x00, 0x01};
    env->SetByteArrayRegion(arr, 0, kHeaderSize, kNaluHeader);

    if (size > 0) {
        env->SetByteArrayRegion(arr,
                                kHeaderSize,
                                static_cast<jsize>(size),
                                reinterpret_cast<const jbyte*>(data.data() + offset));
    }
    return {env, arr};
}

} // namespace FromJVMnamespace ToJVM
 namespace ToJVM

}
 namespace slark::JNI
