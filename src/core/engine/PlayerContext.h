//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <memory>
#include <string>

#include "IEGLContext.h"

namespace slark::engine {

// Player identity and the shared GL context the hardware video decoder needs.
struct PlayerContext {
    std::string playerId;
    std::shared_ptr<slark::IEGLContext> mainGLContext;
};

}  // namespace slark::engine
