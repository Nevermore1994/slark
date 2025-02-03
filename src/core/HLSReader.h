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
    void setupDataProvoider() noexcept;
    void addRequest(http::RequestInfoPtr) noexcept;
    void sendRequest() noexcept;
    void fetchTSData(uint32_t tsIndex) noexcept;
    void sendM3u8Request(const std::string& m3u8Url) noexcept;
    void sendTSRequest(uint32_t index, const std::string& url, Range range) noexcept;
    
    void handleM3u8Header(const http::ResponseHeader& header) noexcept;
    void handleM3u8Data(DataPtr dataPtr) noexcept;
    void handleM3u8Completed() noexcept;
    
    void handleTSHeader(const http::ResponseHeader& header) noexcept;
    void handleTSData(uint32_t index, DataPtr dataPtr) noexcept;
    void handleTSCompleted(uint32_t tsIndex) noexcept;
    void handleError(const http::ErrorInfo& info) noexcept;
private:
    std::atomic_bool isPause_ = false;
    std::atomic_bool isClosed_ = false;
    std::atomic_bool isCompleted_ = false;
    std::atomic_bool isErrorOccurred_ = false;
    std::atomic<uint64_t> receiveLength_ = 0;
    std::mutex requestMutex_;
    std::unique_ptr<Buffer> m3u8Buffer_;
    std::unique_ptr<http::RequestSession> requestSession_;
    std::shared_ptr<HLSDemuxer> demuxer_;
    Synchronized<std::deque<http::RequestInfoPtr>> requestTasks_;
    Synchronized<std::unordered_map<std::string, HLSRequestTaskType>> request;
    Thread worker_;
};

}
