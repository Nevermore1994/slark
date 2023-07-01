//
// Created by Nevermore on 2022/5/12.
// slark IHandler
// Copyright (c) 2022 Nevermore All rights reserved.
//

#include "Data.hpp"
#include "NonCopyable.h"
#include <cstdint>
#include <functional>
#include <string_view>

namespace slark {

enum class IOState {
    Normal,
    Error,
    EndOfFile,
};

using IOHandlerCallBack = std::function<void(std::unique_ptr<Data>, int64_t, IOState)>;

struct IOHandler: public slark::NonCopyable {
    ~IOHandler() override = default;

    virtual bool open(std::string path) = 0;
    virtual void resume() = 0;
    virtual void pause() = 0;
    virtual void close() = 0;
    virtual std::string_view path() const noexcept = 0;
    virtual void setCallBack(IOHandlerCallBack callBack) noexcept = 0;
};

}
