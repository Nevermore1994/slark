//
//  NetReader.h
//  slark
//
//  Created by Nevermore on 2024/12/18.
//
#pragma once

#include "Request.h"
#include "IReader.h"
#include "Synchronized.hpp"
#include "Thread.h"
#include <deque>

namespace slark {

class RemoteReader : public IReader {
public:
    RemoteReader();
    
    ~RemoteReader() override = default;
public:
    bool open(ReaderTaskPtr) noexcept override;
    
    void updateReadRange(Range range) noexcept override;
    
    void reset() noexcept override;
    
    IOState state() noexcept override;
    
    void close() noexcept override;
    
    bool isCompleted() noexcept override;
    
    bool isRunning() noexcept override;
    
    void seek(uint64_t pos) noexcept override;
    
    uint64_t size() noexcept override;
    
private:
    void handleHeader(http::ResponseHeader&& header) noexcept;
    void handleData(DataPtr) noexcept;
    void handleError(const http::ErrorInfo& info) noexcept;
    void handleDisconnect() noexcept;
private:
    std::atomic_bool isClosed_ = false;
    std::atomic_bool isCompleted_ = false;
    std::mutex mutex_;
    bool isErrorOccurred_ = false;
    uint64_t contentLength_ = 0;
    uint64_t receiveLength_ = 0;
    std::unique_ptr<http::Request> link_;
};

}
