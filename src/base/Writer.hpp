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
    Writer(const std::string& path, const std::string& name = "");
    ~Writer() override;
public:
    void close() noexcept override;
    [[nodiscard]] std::string_view path() noexcept override;
    [[nodiscard]] IOState state() noexcept override;
public:
    bool write(DataPtr data) noexcept;
    bool write(Data&& data) noexcept;
    bool write(const Data& data) noexcept;
    bool write(const std::string& str) noexcept;
    bool write(std::string&& str) noexcept;
    uint32_t writeCount() noexcept;
    uint64_t writeSize() noexcept;
    uint32_t getHashId() noexcept;
private:
    void process() noexcept;
private:
    bool isClosed_ = false;
    Time::TimePoint idleTime_ = 0;
    Synchronized<std::vector<DataPtr>> dataList_;
    Synchronized<std::unique_ptr<FileUtil::WriteFile>, std::shared_mutex> file_;
    Thread worker_;
};

}
