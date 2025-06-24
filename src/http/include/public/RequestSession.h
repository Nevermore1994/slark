//
// Created by Nevermore- on 2025/6/24.
//

#pragma once

#include <thread>
#include <atomic>
#include <condition_variable>
#include <deque>
#include "Type.h"
#include "TimerManager.h"

#if ENABLE_HTTPS
#include "HttpsHelper.h"
#endif


namespace slark::http{

class ISocket;

struct Url;

extern void freeSocket(ISocket*) noexcept;

extern void freeUrl(Url*) noexcept;

struct RequestSessionTask {
    std::atomic<bool> isCompleted_ = false;
    std::atomic<bool> isValid_ = true;
    std::atomic<bool> isRedirect_ = false;
    std::unique_ptr<Url, decltype(&freeUrl)> host{nullptr, &freeUrl};
    uint64_t startStamp = 0;
    RequestInfoPtr requestInfo = nullptr;
};

class RequestSession {
public:
    explicit RequestSession(std::unique_ptr<ResponseHandler> handler);

    ~RequestSession();

    void clear() noexcept;

    void close() noexcept;

    std::string request(RequestInfoPtr requestInfo) noexcept;

    bool isBusy() const noexcept {
        return isBusy_;
    }

private:
    void setupSocket() noexcept;

    void redirect(const std::string&) noexcept;

    void process() noexcept;

    int64_t getRemainTime() noexcept;

    bool send() noexcept;

    bool isReceivable() noexcept;

    void receive() noexcept;

    bool parseChunk(DataPtr& data, int64_t& chunkSize, bool& isCompleted) noexcept;

    void onResponseHeader(ResponseHeader&&) noexcept;

    void onResponseData(DataPtr data) noexcept;

    void onError(ResultCode code, int32_t errorCode) noexcept;

    void onCompleted() noexcept;
private:
    std::mutex mutex_;
    std::mutex taskMutex_;
    std::atomic<bool> isExited_ = false;
    std::atomic<bool> isBusy_ = false;
    std::condition_variable cond_;
    std::unique_ptr<ISocket, decltype(&freeSocket)> socket_;
    std::unique_ptr<Url, decltype(&freeUrl)> host_;
    std::unique_ptr<std::thread> worker_ = nullptr;
    std::unique_ptr<ResponseHandler> handler_ = nullptr;
    std::deque<std::unique_ptr<RequestSessionTask>> requestTaskQueue_;
    std::unique_ptr<RequestSessionTask> currentTask_ = nullptr;
};


} // slark

