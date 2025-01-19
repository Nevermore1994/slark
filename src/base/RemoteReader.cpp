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

static const std::string kNetworkReaderPrefixName = "NetReader_";

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
    std::lock_guard<std::mutex> lock(mutex_);
    link_.reset();
    receiveLength_ = 0;
    contentLength_ = 0;
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
    if (task->range.isValid()) {
        info.headers["Range"] = std::format("bytes={}-{}", task_->range.pos, task_->range.end());
    }
    
    http::ResponseHandler handler;
    handler.onData = [this](std::string_view, DataPtr data) {
        handleData(std::move(data));
    };
    handler.onParseHeaderDone = [this](std::string_view, http::ResponseHeader&& header) {
        handleHeader(std::move(header));
    };
    handler.onError = [this](std::string_view, http::ErrorInfo info) {
        handleError(info);
    };
    handler.onDisconnected = [this](std::string_view) {
        handleDisconnect();
    };
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        task_ = std::move(task);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        receiveLength_ = 0;
        link_ = std::make_unique<http::Request>(std::move(info), std::move(handler));
    }
    return true;
}

void RemoteReader::handleData(DataPtr dataPtr) noexcept {
    DataPacket data;
    data.data = std::move(dataPtr);
    data.offset = static_cast<int64_t>(receiveLength_ + task_->range.pos);
    receiveLength_ += data.data->length;
    task_->callBack(std::move(data), state());
}

void RemoteReader::handleHeader(http::ResponseHeader&& header) noexcept {
    if (header.httpStatusCode == http::HttpStatusCode::OK) {
        static const std::string kContentLenth = "Content-Length";
        if (header.headers.contains(kContentLenth)) {
            contentLength_ = std::stoull(header.headers.at(kContentLenth));
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
    task_->callBack(DataPacket(), state());
}

void RemoteReader::handleDisconnect() noexcept {
    isCompleted_ = true;
}

void RemoteReader::updateReadRange(Range range) noexcept {
    if (!link_ || !task_) {
        return;
    }
    link_->cancel();
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
    return !isCompleted_;
}

uint64_t RemoteReader::size() noexcept {
    return contentLength_;
}

void RemoteReader::seek(uint64_t pos) noexcept {
    Range range;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        range = task_->range;
    }
    updateReadRange(range);
}

}
