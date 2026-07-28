//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#include "IOContext.h"

namespace slark::http {

IOContext& IOContext::instance() noexcept {
    static IOContext instance;
    return instance;
}

}  // namespace slark::http
