//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

namespace slark {

enum class NaluFormat {
    AVCC,   // 4字节长度前缀
    AnnexB, // 4字节起始码 00 00 00 01
};

} // namespace slark
