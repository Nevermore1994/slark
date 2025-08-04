//
//  Writer.hpp
//  slark Writer
//
//  Created by Nevermore on 2023/10/20.
//  Copyright (c) 2023 Nevermore All rights reserved.

#pragma once
#include "IOBase.h"
#include "Thread.h"
#include "File.h"
#include "Synchronized.hpp"
#include "MPSCQueue.hpp"

namespace slark {

class Writer: public NonCopyable {
public:
    Writer();
    ~Writer() override;
public:
    bool open(std::string_view path, bool isAppend = false) noexcept;
    
    void close() noexcept;
    
    [[nodiscard]] std::string path() noexcept;
    
    [[nodiscard]] IOState state() noexcept;
    
    void dispose() noexcept;
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
    bool checkState() noexcept;
    
    void process() noexcept;
private:
    std::atomic_bool isOpen_ = false;
    std::atomic_bool isStop_ = false;
    Time::TimePoint idleTime_ = 0;
    MPSCQueue<DataPtr> dataQueue_;
    Synchronized<std::unique_ptr<File::WriteFile>> file_;
    Thread worker_;
};

}
