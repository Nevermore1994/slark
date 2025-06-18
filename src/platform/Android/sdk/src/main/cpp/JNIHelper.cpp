//
// Created by Nevermore on 2025/3/28.
//

#include "JNIHelper.h"
#include "Log.hpp"

namespace slark::JNI {

std::string FromJVM::toString(
    JNIEnv *env,
    jstring str
) noexcept {
    if (str == nullptr) {
        return {};
    }
    auto cStr = env->GetStringUTFChars(
        str,
        nullptr
    );
    if (cStr == nullptr) {
        return {};
    }
    auto string = std::string(cStr);
    env->ReleaseStringUTFChars(
        str,
        cStr
    );
    return string;
}

DataPtr FromJVM::toData(
    JNIEnvPtr env,
    jbyteArray byteArray
) noexcept {
    if (byteArray == nullptr) {
        return nullptr;
    }
    auto length = env->GetArrayLength(byteArray);
    auto dataPtr = std::make_unique<Data>(length);
    env->GetByteArrayRegion(
        byteArray,
        0,
        length,
        reinterpret_cast<jbyte *>(dataPtr->rawData));
    dataPtr->length = length;
    return dataPtr;
}

StringReference ToJVM::toString(
    JNIEnvPtr env,
    std::string_view str
) noexcept {
    jstring jMediaInfo = env->NewStringUTF(str.data());
    return StringReference{env, jMediaInfo};
}

JNIReference<jintArray> ToJVM::toIntArray(
    JNIEnvPtr env,
    const std::vector<int32_t> &data
) noexcept {
    auto size = static_cast<jsize>(data.size());
    jintArray intArray = env->NewIntArray(size);
    if (intArray == nullptr) {
        LogE("create int array error");
        return {env, nullptr};
    }
    env->SetIntArrayRegion(
        intArray,
        0,
        size,
        data.data());
    return {env, intArray};
}

JNIReference<jbyteArray> ToJVM::toByteArray(
    JNIEnvPtr env,
    DataView data
) noexcept {
    auto size = static_cast<int32_t>(data.length());
    jbyteArray dataArray = nullptr;
    if (!data.empty()) {
        dataArray = env->NewByteArray(size);
        if (dataArray == nullptr) {
            LogE("create data array error");
        } else {
            env->SetByteArrayRegion(
                dataArray,
                0,
                size,
                reinterpret_cast<const jbyte *>(data.view()
                    .data()));
        }
    } else {
        dataArray = env->NewByteArray(0);
    }
    return {env, dataArray};
}

JNIReference<jbyteArray> ToJVM::toNaluByteArray(
    JNIEnvPtr env,
    DataView data,
    bool isRawData
) noexcept {
    auto size = static_cast<int32_t>(data.length());
    jbyteArray dataArray = nullptr;
    if (!isRawData && !data.empty()) {
        data = data.substr(4); //remove nalu header
    }
    if (!data.empty()) {
        dataArray = env->NewByteArray(size + 4);
        if (dataArray == nullptr) {
            LogE("create data array error");
        } else {
            Data header(4);
            //big endian
            header.rawData[0] = 0x0;
            header.rawData[1] = 0x0;
            header.rawData[2] = 0x0;
            header.rawData[3] = 0x1; //nalu start code 0x00000001
            header.length = 4;
            env->SetByteArrayRegion(
                dataArray,
                0,
                4,
                reinterpret_cast<jbyte *>(header.rawData)); //nalu header length = 4
            env->SetByteArrayRegion(
                dataArray,
                4,
                size,
                reinterpret_cast<const jbyte *>(data.view()
                    .data()));
        }
    } else {
        dataArray = env->NewByteArray(0);
    }
    return {env, dataArray};
}

} // slark