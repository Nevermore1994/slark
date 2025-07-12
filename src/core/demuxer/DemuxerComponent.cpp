//
// Created by Nevermore- on 2025/7/6.
//

#include "DemuxerComponent.h"
#include "Mp4Demuxer.h"

namespace slark {

DemuxerComponent::DemuxerComponent(DemuxerConfig config)
    : worker_("DemuxerWorker", &DemuxerComponent::demuxData, this)
    , config_(std::move(config)) {
    using namespace std::chrono_literals;
    worker_.setInterval(5ms);
}

DemuxerComponent::~DemuxerComponent() {
    close();
    worker_.stop();
}

void DemuxerComponent::reset() noexcept {
    worker_.pause();
    if (auto demuxer = demuxer_.load()) {
        demuxer->close();
    }
    clearData();
    demuxer_.reset();
    probeBuffer_.reset();
}

void DemuxerComponent::close() noexcept {
    reset();
    isClosed_ = true;
}

void DemuxerComponent::start() noexcept {
    worker_.start();
}

void DemuxerComponent::pause() noexcept {
    worker_.pause();
}

bool DemuxerComponent::pushData(std::list<DataPacket>&& dataList) noexcept {
    dataList_.withLock([dataList = std::move(dataList)](auto& list) mutable {
        list.insert(list.end(),
                    std::make_move_iterator(dataList.begin()),
                    std::make_move_iterator(dataList.end()));
    });
    return true;
}

void DemuxerComponent::demuxData() noexcept {
    if (flushed_) {
        LogI("demuxer is flushed, clear data.");
        clearData();
        return;
    }
    DataPacket demuxData;
    dataList_.withLock([&demuxData](auto& list) {
        if (list.empty()) {
            return;
        }
        demuxData = std::move(list.front());
        list.pop_front();
    });
    if (demuxData.empty()) {
        worker_.pause();
        return;
    }
    flushed_ = false; // Reset flushed state at the start of demuxing
    if (auto demuxer = demuxer_.load(); !demuxer || !demuxer->isOpened()) {
        openDemuxer(demuxData);
        demuxer = demuxer_.load();
        if (demuxer == nullptr) {
            LogE("demuxer is not created.");
            return;
        }
        if (demuxer &&
            demuxer->isOpened() &&
            demuxer->type() == DemuxerType::MP4) {
            DemuxerResult result;
            result.resultCode = DemuxerResultCode::ParsedHeader;
            invokeHandleResultFunc(std::move(result));
            clearData();
        }
    } else if (demuxer) {
        auto result = demuxer->parseData(demuxData);
        invokeHandleResultFunc(std::move(result));
    }
}

bool DemuxerComponent::open() noexcept {
    auto demuxer = demuxer_.load();
    if (!demuxer) {
        LogE("demuxer is not created.");
        return false;
    }
    if (demuxer->isOpened()) {
        LogI("demuxer is already opened.");
        return true;
    }
    auto res = demuxer->open(probeBuffer_);
    auto demuxerType = demuxer->type();
    if (demuxerType == DemuxerType::MP4) {
        handleOpenMp4DemuxerResult(res);
    }
    if (res) {
        probeBuffer_->reset();
    }
    return res;
}

bool DemuxerComponent::openDemuxer(DataPacket& packet) noexcept {
    if (probeBuffer_ == nullptr) {
        probeBuffer_ = std::make_unique<Buffer>();
    }

    if (!probeBuffer_->append(static_cast<uint64_t>(packet.offset), std::move(packet.data))) {
        LogE("append data error:{}", packet.offset);
        return false;
    }

    if (auto demuxer = demuxer_.load(); !demuxer) {
        if (!createDemuxer()) {
            LogE("create demuxer failed.");
            return false;
        }
    }
    return open();
}

bool DemuxerComponent::createDemuxer() noexcept {
    auto dataView = probeBuffer_->view();
    auto demuxerType = DemuxerManager::shareInstance().probeDemuxType(dataView);
    if (demuxerType == DemuxerType::Unknown) {
        return false;
    }

    auto demuxer = DemuxerManager::shareInstance().create(demuxerType);
    if (!demuxer) {
        LogE("not found demuxer type:{}", static_cast<int32_t>(demuxerType));
        return false;
    } else {
        demuxer->init(std::move(config_));
    }
    demuxer_.reset(demuxer);
    return true;
}

void DemuxerComponent::handleOpenMp4DemuxerResult(bool isSuccess) noexcept {
    auto demuxer = demuxer_.load();
    if (!demuxer) {
        LogE("demuxer is nullptr.");
        return;
    }
    auto mp4Demuxer = std::dynamic_pointer_cast<Mp4Demuxer>(demuxer);
    if (isSuccess) {
        auto dataStart = mp4Demuxer->headerInfo()->headerLength + 8; //skip size and type
        Range range;
        range.pos = dataStart;
        range.size = static_cast<int64_t>(mp4Demuxer->headerInfo()->dataSize) - 8; //skip size and type
        mp4Demuxer->seekPos(dataStart);
        invokeSeekFunc(range);
        LogI("mp4 seek to:{}", dataStart);
#if DEBUG
        auto ss = mp4Demuxer->description();
        LogI("mp4 info:{}", ss);
#endif
    } else {
        int64_t moovBoxStart = 0;
        uint32_t moovBoxSize = 0;
        constexpr int64_t kMb = 1024 * 1024;
        if (mp4Demuxer->probeMoovBox(*probeBuffer_, moovBoxStart, moovBoxSize)) {
            return;
        }
        auto pos = static_cast<int64_t>(probeBuffer_->pos());
        if (pos == 0) {
            auto headerInfo = mp4Demuxer->headerInfo();
            if (headerInfo) {
                pos = static_cast<int64_t>(headerInfo->headerLength + headerInfo->dataSize);
            } else {
                pos = static_cast<int64_t>(config_.fileSize) - kMb;
            }
        } else {
            pos -= kMb;
        }
        if (pos < 0) {
            return;
        }
        auto probePos = static_cast<uint64_t>(pos);
        seekToPos(probePos);
        probeBuffer_->reset();
        probeBuffer_->setOffset(probePos);
        invokeSeekFunc(Range(probePos));
    }
}

void DemuxerComponent::seekToPos(uint64_t pos) noexcept {
    if (isClosed_) {
        LogE("demuxer component is closed.");
        return;
    }

    if (auto demuxer = demuxer_.load()) {
        demuxer->seekPos(pos);
        return;
    }
}

uint64_t DemuxerComponent::getSeekToPos(double time) noexcept {
    if (auto demuxer = demuxer_.load()) {
        if (!demuxer->isOpened()) {
            LogE("demuxer is not opened.");
            return 0;
        }
        return demuxer->getSeekToPos(time);
    } else {
        LogE("demuxer is nullptr.");
    }
    return 0;
}
} // slark