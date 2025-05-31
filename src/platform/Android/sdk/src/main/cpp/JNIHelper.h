//
// Created by Nevermore on 2025/3/28.
//

#pragma once
#include "JNIBase.hpp"
#include <string>
#include "Data.hpp"

namespace slark::JNI {

namespace FromJVM {
    std::string toString(JNIEnvPtr env, jstring str) noexcept;

    DataPtr toData(JNIEnvPtr env, jbyteArray byteArray) noexcept;
}

namespace ToJVM {
    StringReference toString(JNIEnvPtr env, std::string_view) noexcept;

    JNIReference<jbyteArray> toByteArray(JNIEnvPtr env, DataView data) noexcept;

    ///write header length
    JNIReference<jbyteArray> toNaluByteArray(JNIEnvPtr env, DataView data, bool isRawData = false) noexcept;

    JNIReference<jintArray> toIntArray(JNIEnvPtr env, const std::vector<int32_t>& data) noexcept;
}

}