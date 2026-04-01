//
//  IOBase.h
//  Pods
//
//  Created by Nevermore on 2024/12/17.
//

#pragma once

#include <cstdint>

namespace slark {

enum class IOState : uint8_t {
    Unknown,
    Normal,
    Pause,
    Error,
    EndOfFile,
    Closed,
};

}
