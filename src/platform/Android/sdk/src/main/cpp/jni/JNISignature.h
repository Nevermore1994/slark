//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <string>
#include <string_view>

namespace slark::JNI {

namespace Sig {
inline constexpr std::string_view Void = "V";
inline constexpr std::string_view Bool = "Z";
inline constexpr std::string_view Byte = "B";
inline constexpr std::string_view Char = "C";
inline constexpr std::string_view Short = "S";
inline constexpr std::string_view Int = "I";
inline constexpr std::string_view Long = "J";
inline constexpr std::string_view Float = "F";
inline constexpr std::string_view Double = "D";
inline constexpr std::string_view String = "Ljava/lang/String;";
inline constexpr std::string_view ByteArray = "[B";
inline constexpr std::string_view IntArray = "[I";
inline constexpr std::string_view ByteBuffer = "Ljava/nio/ByteBuffer;";
inline constexpr std::string_view Object = "Ljava/lang/Object;";
} // namespace Sig

namespace Sig

    inline std::string
    makeArray(std::string_view type) {
return std::string("[").append(type);
}

inline std::string makeObject(std::string_view className) {
    return std::string("L").append(className).append(";");
}

template <typename... Args>
std::string makeSignature(std::string_view ret, Args&&... args) {
    std::string result = "(";
    (result.append(args), ...);
    result.append(")").append(ret);
    return result;
}

} // namespace slark::JNI
namespace slark::JNI
