//
//  Writer.hpp
//  slark Writer
//
//  Created by Nevermore on 2023/10/20.
//  Copyright (c) 2023 Nevermore All rights reserved.

#pragma once

#include "IOHandler.h"
#include "Thread.h"
#include "File.h"
#include "Synchronized.hpp"

namespace slark {

class Writer: public IOHandler {
public:
    Writer();
    ~Writer() override;
public:
    bool open(std::string_view path, bool isAppend = false) noexcept;
    void close() noexcept override;
    [[nodiscard]] std::string_view path() noexcept override;
    [[nodiscard]] IOState state() noexcept override;
    void stop() noexcept;
public:
    bool write(DataPtr data) noexcept;
    bool write(Data&& data) noexcept;
    bool write(const Data& data) noexcept;
    bool write(const std::string& str) noexcept;
    bool write(std::string&& str) noexcept;
    uint32_t writeCount() noexcept;
    uint64_t writeSize() noexcept;
    uint32_t getHashId() noexcept;
    bool isOpen() noexcept {
        return isOpen_;
    }
private:
    void process() noexcept;
private:
    std::atomic_bool isOpen_ = false;
    std::atomic_bool isStop_ = false;
    Time::TimePoint idleTime_ = 0;
    Synchronized<std::vector<DataPtr>> dataList_;
    Synchronized<std::unique_ptr<FileUtil::WriteFile>, std::shared_mutex> file_;
    Thread worker_;
};

}
