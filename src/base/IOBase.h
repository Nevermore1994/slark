//
//  IOBase.h
//  Pods
//
//  Created by Nevermore on 2024/12/17.
//

#pragma once

namespace slark {

enum class IOState {
    Unknown,
    Normal,
    Pause,
    Error,
    EndOfFile,
    Closed,
};

}
