//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

namespace slark {

enum class NaluFormat {
    AVCC,    // 4-byte length prefix
    AnnexB,  // 4-byte start code 00 00 00 01
};

}  // namespace slark
