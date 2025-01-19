//
//  HLSReader.cpp
//  slark
//
//  Created by Nevermore on 2024/12/25.
//

#include "HLSReader.h"
#include "Log.hpp"
#include "Util.hpp"

namespace slark {

HLSReader::HLSReader()
     :worker_(Util::genRandomName("HLSRead_"), &HLSReader::sendRequest, this){
    
}

void HLSReader::handleTSData(DataPtr dataPtr) noexcept {
    DataPacket data;
    data.data = std::move(dataPtr);
    data.offset = static_cast<int64_t>(receiveLength_);
    data.tag = std::to_string(currentTSIndex_);
    receiveLength_ += data.data->length;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        task_->callBack(std::move(data), state());
    }
}

void HLSReader::handleTSHeader(http::ResponseHeader&& header) noexcept {
    if (header.httpStatusCode == http::HttpStatusCode::OK) {
        return;
    }
    http::ErrorInfo info;
    info.errorCode = static_cast<int>(header.httpStatusCode);
    info.retCode = http::ResultCode::ConnectGenericError;
    handleTSError(info);
}

void HLSReader::handleTSError(const http::ErrorInfo& info) noexcept {
    LogE("ts data http code:{} error code:{}", info.errorCode, static_cast<int>(info.retCode));
    isErrorOccurred_ = true;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        task_->callBack(DataPacket(), state());
    }
}

void HLSReader::handleTSDisconnect() noexcept {
    
}

void HLSReader::handleM3u8Data(DataPtr data) noexcept {
    if (!m3u8Buffer_->append(std::move(data))) {
        LogI("append m3u8 data error");
        return;
    }
    if (!demuxer_->open(m3u8Buffer_)) {
        LogI("need more m3u8 data.");
        return;
    }
    m3u8Buffer_.reset();
    if (demuxer_->isPlayList()) {
        auto listSize = demuxer_->playListInfos().size();
        auto playList = demuxer_->playListInfos()[listSize / 2];
        sendM3u8Request(playList.m3u8Url);
        LogI("play list, resend m3u8 req:{}, url:{}", playList.name, playList.m3u8Url);
    } else {
        fetchTSDataInOrder();
    }
}

void HLSReader::handleM3u8Error(const http::ErrorInfo &info) noexcept {
    LogE("m3u8 error http code:{} error code:{}", info.errorCode, static_cast<int>(info.retCode));
    isErrorOccurred_ = true;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        task_->callBack(DataPacket(), state());
    }
}

void HLSReader::sendM3u8Request(const std::string& m3u8Url) noexcept {
    http::RequestInfo info;
    info.url = m3u8Url;
    info.methodType = http::HttpMethodType::Get;
    http::ResponseHandler handler;
    handler.onData = [this](std::string_view, DataPtr data) {
        handleM3u8Data(std::move(data));
    };
    handler.onError = [this](std::string_view, http::ErrorInfo info) {
        handleM3u8Error(info);
    };
    m3u8Buffer_ = std::make_unique<Buffer>();
    auto task = std::make_unique<HLSRequestTask>();
    task->info = std::move(info);
    task->handler = std::move(handler);
    task->type = HLSRequestTaskType::M3U8;
    addRequest(std::move(task));
}

bool HLSReader::open(ReaderTaskPtr task) noexcept {
    if (isClosed_ || !task) {
        return false;
    }
    auto m3u8Url = task->path;
    DemuxerConfig config;
    config.filePath = m3u8Url;
    demuxer_->init(config);
    sendM3u8Request(m3u8Url);
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        task_ = std::move(task);
    }
    return true;
}

void HLSReader::sendTSRequest(const std::string& url, Range range) noexcept {
    http::RequestInfo info;
    info.url = url;
    info.methodType = http::HttpMethodType::Get;
    if (range.isValid()) {
        info.headers["Range"] = std::format("bytes={}-{}", range.pos, range.end());
    }
    
    http::ResponseHandler handler;
    handler.onData = [this](std::string_view, DataPtr data) {
        handleTSData(std::move(data));
    };
    handler.onParseHeaderDone = [this](std::string_view, http::ResponseHeader&& header) {
        handleTSHeader(std::move(header));
    };
    handler.onError = [this](std::string_view, http::ErrorInfo info) {
        handleTSError(info);
    };
    handler.onDisconnected = [this](std::string_view) {
        handleTSDisconnect();
    };
    auto task = std::make_unique<HLSRequestTask>();
    task->info = std::move(info);
    task->handler = std::move(handler);
    task->type = HLSRequestTaskType::TS;
    addRequest(std::move(task));
}

void HLSReader::updateReadRange(Range) noexcept {
    //nothing
}

IOState HLSReader::state() noexcept {
    if (isClosed_) {
        return IOState::Closed;
    }
    
    if (isErrorOccurred_) {
        return IOState::Error;
    }
    
    if (isCompleted_) {
        return IOState::EndOfFile;
    }
    
    if (isPause_) {
        return IOState::Pause;
    }
    return IOState::Normal;
}

void HLSReader::setDemuxer(std::shared_ptr<HLSDemuxer> demuxer) noexcept {
    demuxer_ = demuxer;
}

void HLSReader::fetchTSDataInOrder() noexcept {
    currentTSIndex_++;
    const auto& tsInfos = demuxer_->getTSInfos();
    if (currentTSIndex_ < 0 ||
        currentTSIndex_ >= static_cast<int32_t>(tsInfos.size())) {
        isCompleted_ = true;
        return;
    }
    auto index = static_cast<uint32_t>(currentTSIndex_);
    const auto& info = tsInfos[index];
    sendTSRequest(info.url, info.range);
}

void HLSReader::reset() noexcept {
    isCompleted_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        m3u8Buffer_.reset();
        demuxer_.reset();
        currentTSIndex_ = -1;
        isErrorOccurred_ = false;
        receiveLength_ = 0;
        link_.reset();
        task_.reset();
    }
    requestTasks_.withLock([](auto& tasks) {
        tasks.clear();
    });
    worker_.pause();
}

void HLSReader::close() noexcept {
    reset();
    worker_.stop();
}

void HLSReader::start() noexcept {
    isPause_ = false;
    worker_.start();
}

void HLSReader::pause() noexcept {
    worker_.pause();
    isPause_ = true;
}

bool HLSReader::isCompleted() noexcept {
    return isCompleted_;
}

bool HLSReader::isRunning() noexcept {
    return !isPause_;
}

uint64_t HLSReader::size() noexcept {
    return 0;
}

void HLSReader::seek(uint64_t pos) noexcept {
    uint32_t tsIndex = 0;
    const auto& infos = demuxer_->getTSInfos();
    for(uint32_t i = 0; i < infos.size(); i++) {
        if (infos[i].range.pos <= pos && pos < infos[i].range.end()) {
            tsIndex = i;
            break;
        }
    }
    currentTSIndex_ = static_cast<int32_t>(tsIndex) - 1;
    fetchTSDataInOrder();
}

void HLSReader::addRequest(RequestTaskPtr task) noexcept {
    requestTasks_.withLock([&task](auto& tasks){
        tasks.push_back(std::move(task));
    });
    worker_.start();
}

void HLSReader::sendRequest() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if ((link_ && !link_->isCompleted()) || isPause_) {
            worker_.pause();
            return;
        }
    }
    RequestTaskPtr task;
    requestTasks_.withLock([&task](auto& tasks){
        if (!tasks.empty()) {
            task = std::move(tasks.front());
            tasks.pop_front();
        }
    });
    if (!task) {
        worker_.pause();
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    receiveLength_ = 0;
    const auto& tsInfos = demuxer_->getTSInfos();
    if (task->type == HLSRequestTaskType::TS &&
        0 <= currentTSIndex_ &&
        currentTSIndex_ < static_cast<int32_t>(tsInfos.size())) {
        auto index = static_cast<uint32_t>(currentTSIndex_);
        const auto& info = demuxer_->getTSInfos()[index];
        if (info.range.isValid()) {
            receiveLength_ = info.range.pos;
        }
    }
    isErrorOccurred_ = false;
    link_ = std::make_unique<http::Request>(std::move(task->info), std::move(task->handler));
}

}
