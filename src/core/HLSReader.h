//
//  HLSReader.h
//  slark
//
//  Created by Nevermore on 2024/12/25.
//
#pragma once

#include "IReader.h"
#include "Request.h"
#include "Buffer.hpp"
#include "HLSDemuxer.h"
#include "Thread.h"

namespace slark {

enum class HLSRequestTaskType {
    M3U8,
    TS
};

struct HLSRequestTask {
    http::RequestInfo info;
    http::ResponseHandler handler;
    HLSRequestTaskType type = HLSRequestTaskType::M3U8;
};

using RequestTaskPtr = std::unique_ptr<HLSRequestTask>;

class HLSReader : public IReader {
public:
    HLSReader();
    
    ~HLSReader() override = default;
    
    bool open(ReaderTaskPtr) noexcept override;
    
    void updateReadRange(Range range) noexcept override;
    
    IOState state() noexcept override;
    
    void reset() noexcept override;
    
    void close() noexcept override;
    
    void start() noexcept override;
    
    void pause() noexcept override;
    
    bool isCompleted() noexcept override;
    
    bool isRunning() noexcept override;
    
    void seek(uint64_t pos) noexcept override;
    
    uint64_t size() noexcept override;
    
    void setDemuxer(std::shared_ptr<HLSDemuxer> demuxer) noexcept;
private:
    void addRequest(RequestTaskPtr) noexcept;
    void sendRequest() noexcept;
    void fetchTSDataInOrder() noexcept;
    void sendM3u8Request(const std::string& m3u8Url) noexcept;
    void sendTSRequest(const std::string& url, Range range) noexcept;
    
    void handleM3u8Error(const http::ErrorInfo& info) noexcept;
    void handleM3u8Data(DataPtr) noexcept;
    
    void handleTSHeader(http::ResponseHeader&& header) noexcept;
    void handleTSData(DataPtr) noexcept;
    void handleTSError(const http::ErrorInfo& info) noexcept;
    void handleTSDisconnect() noexcept;
private:
    int32_t currentTSIndex_ = -1;
    std::atomic_bool isPause_ = false;
    std::atomic_bool isClosed_ = false;
    std::atomic_bool isCompleted_ = false;
    bool isErrorOccurred_ = false;
    uint64_t receiveLength_ = 0;
    std::mutex mutex_;
    std::unique_ptr<Buffer> m3u8Buffer_;
    std::unique_ptr<http::Request> link_;
    std::shared_ptr<HLSDemuxer> demuxer_;
    Synchronized<std::deque<RequestTaskPtr>> requestTasks_;
    Thread worker_;
};

}
