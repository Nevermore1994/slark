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

namespace IO {

constexpr uint32_t kReadDefaultSize = 1024 * 5;

}

using IOHandlerCallBack = std::function<void(std::unique_ptr<Data>, int64_t, IOState)>;

struct IOHandler: public slark::NonCopyable {
    ~IOHandler() override = default;

    virtual IOState state() const noexcept = 0;
    virtual bool open(const std::string& path) noexcept = 0;
    virtual void resume() noexcept = 0;
    virtual void pause() noexcept = 0;
    virtual void close() noexcept = 0;
    virtual std::string_view path() const noexcept = 0;
    virtual void setCallBack(IOHandlerCallBack callBack) noexcept = 0;
};

}
