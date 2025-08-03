//
//  HLSReader.cpp
//  slark
//
//  Created by Nevermore on 2024/12/25.
//

#include "HLSReader.h"

#include <utility>
#include "Log.hpp"
#include "Util.hpp"

namespace slark {

static constexpr std::string_view kM3u8Tag = "HLSRequestM3u8";
static constexpr std::string_view kTsTag = "HLSRequestTs";

bool parseTsTag(const std::string& tag, uint32_t& tsIndex) noexcept {
    auto pos = tag.find('_');
    if (pos == std::string::npos) {
        return false;
    }
    auto index = tag.substr(pos + 1);
    tsIndex = static_cast<uint32_t>(std::stoi(index));
    return true;
}

HLSReader::HLSReader()
    : m3u8Buffer_(std::make_unique<Buffer>())
    , worker_(Util::genRandomName("HLSRead_"), &HLSReader::sendRequest, this) {
    type_ = ReaderType::HLS;
}

void HLSReader::handleError(const http::ErrorInfo&) noexcept {
    isErrorOccurred_ = true;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        task_->callBack(this, DataPacket(), state());
    }
}

void HLSReader::handleTSData(uint32_t index, DataPtr dataPtr) noexcept {
    DataPacket data;
    data.data = std::move(dataPtr);
    data.offset = static_cast<int64_t>(receiveLength_);
    data.tag = std::to_string(index);
    receiveLength_ += data.data->length;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        task_->callBack(this, std::move(data), state());
    }
}

void HLSReader::handleTSCompleted(uint32_t tsIndex) noexcept {
    fetchTSData(tsIndex + 1);
    LogI("fetch ts index: {}", tsIndex + 1);
}

void HLSReader::handleM3u8Data(DataPtr data) noexcept {
    if (!m3u8Buffer_->append(std::move(data))) {
        LogI("append m3u8 data error");
        return;
    }
    
    std::lock_guard<std::mutex> lock(taskMutex_);
    if (demuxer_->open(m3u8Buffer_)) {
        LogI("parse m3u8 data success.");
    }
}

void HLSReader::handleM3u8Completed() noexcept {
    m3u8Buffer_->reset();
    bool isPlayList = false;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        isPlayList = demuxer_->isPlayList() && !demuxer_->isOpened();
    }
    if (!isPlayList) {
        fetchTSData(0);
        return;
    }
    PlayListInfo playInfo;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        const auto& playList = demuxer_->playListInfos();
        playInfo = playList.front();
    }
    if (!playInfo.m3u8Url.empty()) {
        sendM3u8Request(playInfo.m3u8Url);
        LogI("play list, resend m3u8 req:{}, url:{}", playInfo.name, playInfo.m3u8Url);
    } else {
        LogE("play list is empty!");
    }
}

void HLSReader::sendM3u8Request(const std::string& m3u8Url) noexcept {
    auto info = std::make_unique<http::RequestInfo>();
    info->url = m3u8Url;
    info->methodType = http::HttpMethodType::Get;
    info->tag = kM3u8Tag;
    addRequest(std::move(info));
    LogI("send m3u8 request:{}", m3u8Url);
}

void HLSReader::setupDataProvider() noexcept {
    std::lock_guard<std::mutex> lock(requestMutex_);
    if (requestSession_) {
        requestSession_->close();
        requestSession_.reset();
    }
    auto handler = std::make_unique<http::ResponseHandler>();
    handler->onParseHeaderDone = [this] (const http::RequestInfo& info,
                                         http::ResponseHeader&& header) {
        if (header.httpStatusCode == http::HttpStatusCode::OK) {
            return;
        }
        http::ErrorInfo errorInfo;
        errorInfo.errorCode = static_cast<int>(header.httpStatusCode);
        errorInfo.retCode = http::ResultCode::ConnectGenericError;
        handleError(errorInfo);
        LogE("{}, http code:{} error code:{}", info.tag, errorInfo.errorCode, static_cast<int>(errorInfo.retCode));
    };
    handler->onData = [this] (const http::RequestInfo& info,
                              DataPtr data) {
        if (info.tag == kM3u8Tag) {
            handleM3u8Data(std::move(data));
        } else {
            uint32_t tsIndex = 0;
            if (!parseTsTag(info.tag, tsIndex)) {
                LogE("error tag:{}", info.tag);
                return;
            }
            handleTSData(tsIndex, std::move(data));
        }
    };
    handler->onError = [this] (const http::RequestInfo& info,
                               http::ErrorInfo errorInfo) {
        handleError(errorInfo);
        LogE("{}, http code:{} error code:{}", info.tag, errorInfo.errorCode, static_cast<int>(errorInfo.retCode));
    };
    handler->onCompleted = [this] (const http::RequestInfo& info) {
        if (info.tag == kM3u8Tag) {
            handleM3u8Completed();
        } else {
            uint32_t tsIndex = 0;
            if (!parseTsTag(info.tag, tsIndex)) {
                LogE("error tag:{}", info.tag);
                return;
            }
            handleTSCompleted(tsIndex);
        }
    };
    
    requestSession_ = std::make_unique<http::RequestSession>(std::move(handler));
}

bool HLSReader::open(ReaderTaskPtr task) noexcept {
    if (isClosed_ || !task) {
        return false;
    }
    setupDataProvider();
    auto m3u8Url = task->path;
    DemuxerConfig config;
    config.filePath = m3u8Url;
    demuxer_->init(config);
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        task_ = std::move(task);
    }
    sendM3u8Request(m3u8Url);
    return true;
}

void HLSReader::sendTSRequest(uint32_t index, const std::string& url, Range range) noexcept {
    auto info = std::make_unique<http::RequestInfo>();
    info->url = url;
    info->methodType = http::HttpMethodType::Get;
    if (range.isValid()) {
        info->headers["Range"] = range.toHeaderString();
    }
    
    info->tag = std::format("{}_{}", kTsTag, std::to_string(index));
    receiveLength_ = range.start();
    addRequest(std::move(info));
    LogI("send ts request:{}, url:{}, range:{}", index, url, range.toString());
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
    std::lock_guard<std::mutex> lock(taskMutex_);
    demuxer_ = std::move(demuxer);
}

void HLSReader::fetchTSData(uint32_t tsIndex) noexcept {
    const auto& tsInfos = demuxer_->getTSInfos();
    if (tsIndex >= tsInfos.size()) {
        isCompleted_ = true;
        LogI("hls read completed");
        return;
    }
    LogI("fetchTSData:{}", tsIndex);
    const auto& info = tsInfos[tsIndex];
    sendTSRequest(tsIndex, info.url, info.range);
}

void HLSReader::reset() noexcept {
    isCompleted_ = false;
    isErrorOccurred_ = false;
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        requestSession_->close();
        requestSession_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        task_.reset();
        demuxer_.reset();
    }
    receiveLength_ = 0;
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
    setupDataProvider();
    requestTasks_.withLock([](auto& tasks) {
        tasks.clear();
    });
    auto tsIndex = static_cast<size_t>(pos);
    const auto& infos = demuxer_->getTSInfos();
    if (0 <= tsIndex && tsIndex < infos.size()) {
        fetchTSData(static_cast<uint32_t>(tsIndex));
        worker_.start();
    } else {
        LogE("seek error:{}, info size:{}", tsIndex, infos.size());
    }
    LogI("seek to ts index:{}", tsIndex);
}

void HLSReader::addRequest(http::RequestInfoPtr task) noexcept {
    requestTasks_.withLock([&task](auto& tasks){
        tasks.push_back(std::move(task));
    });
    if (!isPause_) {
        worker_.start();
    }
}

void HLSReader::sendRequest() noexcept {
    {
        std::lock_guard<std::mutex> lock(requestMutex_);
        if ((requestSession_ && requestSession_->isBusy()) || isPause_ || isErrorOccurred_) {
            worker_.pause();
            return;
        }
    }
    http::RequestInfoPtr task = nullptr;
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
    isErrorOccurred_ = false;
    std::lock_guard<std::mutex> lock(requestMutex_);
    requestSession_->request(std::move(task));
}

}
