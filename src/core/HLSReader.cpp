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

void HLSReader::handleTSData(uint32_t index, DataPtr dataPtr) noexcept {
    DataPacket data;
    data.data = std::move(dataPtr);
    data.offset = static_cast<int64_t>(receiveLength_);
    data.tag = std::to_string(index);
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

void HLSReader::handleTSRequestDisconnect(uint32_t tsIndex) noexcept {
    fetchTSData(tsIndex + 1);
}

void HLSReader::handleM3u8Data(DataPtr data) noexcept {
    if (!m3u8Buffer_->append(std::move(data))) {
        LogI("append m3u8 data error");
        return;
    }
    
    if (demuxer_->open(m3u8Buffer_)) {
        LogI("parse m3u8 data success.");
    }
}

void HLSReader::handleM3u8RequestDisconnect() noexcept {
    m3u8Buffer_.reset();
    if (demuxer_->isPlayList() && !demuxer_->isOpened()) {
        auto listSize = demuxer_->playListInfos().size();
        auto playList = demuxer_->playListInfos()[listSize / 2];
        sendM3u8Request(playList.m3u8Url);
        LogI("play list, resend m3u8 req:{}, url:{}", playList.name, playList.m3u8Url);
    } else {
        fetchTSData(0);
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
    handler.onDisconnected = [this](std::string_view) {
        handleM3u8RequestDisconnect();
    };
    m3u8Buffer_ = std::make_unique<Buffer>();
    auto task = std::make_unique<HLSRequestTask>();
    task->info = std::move(info);
    task->handler = std::move(handler);
    task->type = HLSRequestTaskType::M3U8;
    addRequest(std::move(task));
    LogI("send m3u8 request:{}", m3u8Url);
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

void HLSReader::sendTSRequest(uint32_t index, const std::string& url, Range range) noexcept {
    http::RequestInfo info;
    info.url = url;
    info.methodType = http::HttpMethodType::Get;
    if (range.isValid()) {
        info.headers["Range"] = std::format("bytes={}-{}", range.pos, range.end());
    }
    
    http::ResponseHandler handler;
    handler.onData = [index, this](std::string_view, DataPtr data) {
        handleTSData(index, std::move(data));
    };
    handler.onParseHeaderDone = [this](std::string_view, http::ResponseHeader&& header) {
        handleTSHeader(std::move(header));
    };
    handler.onError = [this](std::string_view, http::ErrorInfo info) {
        handleTSError(info);
    };
    handler.onDisconnected = [index, this](std::string_view) {
        handleTSRequestDisconnect(index);
    };
    auto task = std::make_unique<HLSRequestTask>();
    task->info = std::move(info);
    task->handler = std::move(handler);
    task->type = HLSRequestTaskType::TS;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        receiveLength_ = range.isValid() ? range.pos : 0;
    }
    addRequest(std::move(task));
    LogI("send ts request:{}, url:{}, range:[{}, {}]", index, url, range.pos, range.size);
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

void HLSReader::fetchTSData(uint32_t tsIndex) noexcept {
    const auto& tsInfos = demuxer_->getTSInfos();
    if (tsIndex < 0 ||
        tsIndex >= static_cast<int32_t>(tsInfos.size())) {
        isCompleted_ = true;
        return;
    }
    const auto& info = tsInfos[tsIndex];
    sendTSRequest(tsIndex, info.url, info.range);
}

void HLSReader::reset() noexcept {
    isCompleted_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        m3u8Buffer_.reset();
        demuxer_.reset();
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
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (link_) {
            link_->cancel();
        }
    }
    requestTasks_.withLock([](auto& tasks) {
        tasks.clear();
    });
    auto tsIndex = static_cast<size_t>(pos);
    const auto& infos = demuxer_->getTSInfos();
    if (0 <= tsIndex && tsIndex < infos.size()) {
        fetchTSData(tsIndex);
        worker_.start();
    } else {
        LogE("seek error:{}, info size:{}", tsIndex, infos.size());
    }
    LogI("seek to ts index:{}", tsIndex);
}

void HLSReader::addRequest(RequestTaskPtr task) noexcept {
    requestTasks_.withLock([&task](auto& tasks){
        tasks.push_back(std::move(task));
    });
    if (!isPause_) {
        worker_.start();
    }
}

void HLSReader::sendRequest() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if ((link_ && !link_->isCompleted()) || isPause_) {
            worker_.pause();
            return;
        }
    }
    link_.reset();
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
    isErrorOccurred_ = false;
    link_ = std::make_unique<http::Request>(std::move(task->info), std::move(task->handler));
}

}
