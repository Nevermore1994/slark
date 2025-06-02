//
// Created by Nevermore on 2025/3/27.
//

#pragma once

#include "JNIBase.hpp"

namespace slark::JNI {

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
inline constexpr std::string_view Object = "Ljava/lang/Object;";

inline std::string makeArray(std::string_view type) {
    return std::string("[").append(type);
}

inline std::string makeObject(std::string_view type) {
    return std::string("L").append(type)
        .append(";");
}

template<typename ... Args>
std::string makeJNISignature(
    std::string_view ret,
    Args &&... args
) {
    std::string res = "(";
    auto appendFunc = [&](std::string_view arg) {
        res += arg;
    };
    (appendFunc(args), ...);
    res += ")";
    res += ret;
    return res;
}

} // slark::JNI

