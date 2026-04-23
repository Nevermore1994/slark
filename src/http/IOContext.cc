//
// Created by Nevermore on 2025/4/1.
// Slark IOContext
// Copyright (c) 2025 Nevermore All rights reserved.
//
#include "IOContext.h"

namespace slark::http {

IOContext& IOContext::instance() noexcept {
    static IOContext instance;
    return instance;
}

} // namespace slark::http
