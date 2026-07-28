//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once
#include <format>
#include <print>
#include <string_view>

namespace slark {

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#ifdef DEBUG

#define SAssert(result, format, ...)  \
    do {                              \
        if (!(result)) [[unlikely]] { \
            std::print(stderr,                             \
                       "[Assert] {}:{} ({}) " format "
", \
                       __FILE_NAME__,                      \
                       __LINE__,                           \
                       __FUNCTION__,                       \
                       ##__VA_ARGS__);                     \
            __builtin_trap();                              \
        }                                                  \
    } while (0)

#else

#define SAssert(result, format, ...) /*nothing*/

#endif  // DEBUG

#ifdef __clang__
#pragma clang diagnostic pop
#endif

}  // namespace slark
