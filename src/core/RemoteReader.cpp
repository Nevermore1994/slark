//
//  RemoteReader.cpp
//  slark
//
//  Created by Nevermore on 2024/12/18.
//

#include "RemoteReader.h"
#include "Util.hpp"
#include "Log.hpp"

namespace slark {

RemoteReader::RemoteReader() {
    type_ = ReaderType::NetWork;
}

IOState RemoteReader::state() noexcept {
    if (!link_) {
        return IOState::Closed;
    }
    if (isErrorOccurred_) {
        return IOState::Error;
    }
    return link_->isValid() ? IOState::Normal : (link_->isCompleted() ? IOState::EndOfFile : IOState::Error);
}

void RemoteReader::close() noexcept  {
    isClosed_ = true;
    reset();
}

void RemoteReader::reset() noexcept {
    {
        std::lock_guard<std::mutex> lock(linkMutex_);
        link_.reset();
    }
    receiveLength_ = 0;
    isCompleted_ = false;
    contentLength_.reset();
    isErrorOccurred_ = false;
}

bool RemoteReader::open(ReaderTaskPtr task) noexcept {
    if (isClosed_ || !task) {
        return false;
    }
    reset();
    http::RequestInfo info;
    info.url = task->path;
    info.methodType = http::HttpMethodType::Get;
    auto range = task->range;
    if (range.isValid()) {
        info.headers["Range"] = range.toHeaderString();
    }
    
    http::ResponseHandler handler;
    handler.onData = [this](const http::RequestInfo&,
                            DataPtr data) {
        handleData(std::move(data));
    };
    handler.onParseHeaderDone = [this](const http::RequestInfo&,
                                       http::ResponseHeader&& header) {
        handleHeader(std::move(header));
    };
    handler.onError = [this](const http::RequestInfo&,
                             http::ErrorInfo errorInfo) {
        handleError(errorInfo);
    };
    
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        task_ = std::move(task);
    }
    {
        std::lock_guard<std::mutex> lock(linkMutex_);
        receiveLength_ = 0;
        link_ = std::make_unique<http::Request>(std::move(info), std::move(handler));
    }
    return true;
}

void RemoteReader::handleData(DataPtr dataPtr) noexcept {
    Range range;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        if (task_) {
            range = task_->range;
        }
    }
    DataPacket data;
    data.data = std::move(dataPtr);
    data.offset = static_cast<int64_t>(receiveLength_ + range.start());
    receiveLength_ += data.data->length;
    task_->callBack(this, std::move(data), state());
    if (receiveLength_ >= contentLength_) {
        isCompleted_ = true;
        LogI("RemoteReader read completed, total length:{}", receiveLength_);
    }
}

void RemoteReader::handleHeader(http::ResponseHeader&& header) noexcept {
    if (http::HttpStatusCode::OK <= header.httpStatusCode &&
        header.httpStatusCode < http::HttpStatusCode::IMUsed) {
        static const std::string kContentLength = "Content-Length";
        if (header.headers.contains(kContentLength)) {
            contentLength_ = std::stoull(header.headers.at(kContentLength));
        }
        return;
    }
    http::ErrorInfo info;
    info.errorCode = static_cast<int>(header.httpStatusCode);
    info.retCode = http::ResultCode::ConnectGenericError;
    handleError(info);
}

void RemoteReader::handleError(const http::ErrorInfo& info) noexcept {
    LogE("http code:{} error code:{}", info.errorCode, static_cast<int>(info.retCode));
    isErrorOccurred_ = true;
    task_->callBack(this, DataPacket(), state());
}

void RemoteReader::handleDisconnect() noexcept {

}

void RemoteReader::updateReadRange(Range range) noexcept {
    if (!task_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(linkMutex_);
        if (link_) {
            link_->cancel();
            link_.reset();
        }
    }
    isCompleted_ = false;
    ReaderTaskPtr ptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        ptr = std::move(task_);
    }
    ptr->range = range;
    open(std::move(ptr));
}

bool RemoteReader::isCompleted() noexcept {
    return isCompleted_;
}

bool RemoteReader::isRunning() noexcept {
    std::lock_guard<std::mutex> lock(linkMutex_);
    if (link_) {
        return link_->isValid() && !link_->isCompleted();
    }
    return false;
}

uint64_t RemoteReader::size() noexcept {
    return contentLength_.value_or(0);
}

void RemoteReader::seek(uint64_t pos) noexcept {
    Range range;
    LogI("seek pos:{}", pos);
    range = Range(pos);
    isCompleted_ = false;
    updateReadRange(range);
}

void RemoteReader::start() noexcept {
    {
        std::lock_guard<std::mutex> lock(linkMutex_);
        if (link_ && link_->isValid()) {
            return; //already started
        }
        if (isCompleted_) {
            LogI("RemoteReader is completed, no need to start again.");
            return;
        }
    }
    Range range;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        if (task_) {
            range = task_->range;
        }
    }
    range.shift(static_cast<int64_t>(receiveLength_));
    LogI("start read, range:{}", range.toString());
    updateReadRange(range);
}

void RemoteReader::pause() noexcept {
    std::lock_guard<std::mutex> lock(linkMutex_);
    if (link_) {
        link_->cancel();
        link_.reset();
    }
}

}
