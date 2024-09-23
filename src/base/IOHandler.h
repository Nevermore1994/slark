//
// Created by Nevermore on 2022/5/12.
// slark IHandler
// Copyright (c) 2022 Nevermore All rights reserved.
//

#pragma once
#include "Data.hpp"
#include "NonCopyable.h"

namespace slark {

enum class IOState {
    Unknown,
    Normal,
    Pause,
    Error,
    EndOfFile,
    Closed,
};

struct IOHandler: public NonCopyable {
    ~IOHandler() override = default;

    //get now reader/writer state
    virtual IOState state() noexcept = 0;
    //close now file reader/writer
    virtual void close() noexcept = 0;
    //get now reader/writer path
    virtual std::string_view path() noexcept = 0;
};

}
