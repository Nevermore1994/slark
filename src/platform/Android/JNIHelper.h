//
// Created by Nevermore on 2025/3/28.
//

#pragma once
#include "JNIBase.hpp"
#include <string>
#include "Data.hpp"

namespace slark::JNI {

namespace FromJVM {
    static std::string toString(JNIEnvPtr env, jstring str) noexcept;

    static DataPtr toData(JNIEnvPtr env, jbyteArray byteArray) noexcept;
}

namespace ToJVM {
    static StringReference toString(JNIEnvPtr env, const std::string& ) noexcept;

    static JNIReference<jbyteArray> toByteArray(JNIEnvPtr env, const Data& data) noexcept;

    ///write header length
    static JNIReference<jbyteArray> toNaluByteArray(JNIEnvPtr env, const Data& data) noexcept;
}

}