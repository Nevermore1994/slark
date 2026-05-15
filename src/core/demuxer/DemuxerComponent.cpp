//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DemuxerComponent.h"

#include "Mp4Demuxer.h"

#ifdef SLARK_PERF_ENABLED
#include "Perf.h"
#endif

namespace slark {

DemuxerComponent::DemuxerComponent(DemuxerConfig config)
    : worker_(Thread::create("DemuxerWorker")), config_(std::move(config)) {}

DemuxerComponent::~DemuxerComponent() {
    close();
    worker_->stop();
}

void DemuxerComponent::reset() noexcept {
    worker_->pause();
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
    worker_->start();
}

void DemuxerComponent::pause() noexcept {
    worker_->pause();
}

bool DemuxerComponent::pushData(std::list<DataPacket>&& dataList) noexcept {
    dataList_.withLock([dataList = std::move(dataList)](auto& list) mutable {
        list.insert(list.end(),
                    std::make_move_iterator(dataList.begin()),
                    std::make_move_iterator(dataList.end()));
    });
    worker_->schedule([this]() {
        demuxData();
    });
    return true;
}

void DemuxerComponent::demuxData() noexcept {
    if (flushed_) {
        LogI("demuxer is flushed, clear data.");
        clearData();
        return;
    }
    DataPacket packet;
    dataList_.withLock([&packet](auto& list) {
        if (list.empty()) {
            return;
        }
        packet = std::move(list.front());
        list.pop_front();
    });
    if (packet.empty()) {
        // 队列为空，直接返回；等待 pushData() 或 setInputCompleted() 通过 schedule 再次触发
        return;
    }
    auto demuxer = demuxer_.load();
    if (!demuxer || !demuxer->isOpened()) {
        openDemuxer(packet);
    } else {
#ifdef SLARK_PERF_ENABLED
        auto session = session_.lock();
        auto t0 = Time::nowTimeStamp().point();
#endif
        auto result = demuxer->parseData(packet);
#ifdef SLARK_PERF_ENABLED
        auto durationUs = static_cast<int64_t>(Time::nowTimeStamp().point() - t0);
        SLARK_TRACE_RECORD(session.get(), "demux", durationUs);
#endif
        invokeHandleResultFunc(std::move(result));
    }

    // 队列仍有数据时继续调度，避免等待下次 pushData
    bool hasMore = dataList_.withLock([](auto& list) {
        return !list.empty();
    });
    if (hasMore) {
        worker_->schedule([this]() {
            demuxData();
        });
    }
}

bool DemuxerComponent::open() noexcept {
    auto demuxer = demuxer_.load();
    if (!demuxer) {
        LogE("demuxer is not created.");
        return false;
    }
    if (demuxer->isOpened()) {
        return true;
    }
    if (!demuxer->open(probeBuffer_)) {
        // open 失败，可能需要继续探测（如 MP4 moov box 在文件末尾）
        if (auto headerInfo = demuxer->headerInfo(); headerInfo && headerInfo->headerLength > 0) {
            auto pos = headerInfo->headerLength;
            seekToPos(pos);
            probeBuffer_->reset();
            probeBuffer_->setOffset(pos);
            invokeSeekFunc(Range(pos, 0));
        }
        return false;
    }

    if (auto headerInfo = demuxer->headerInfo(); headerInfo && headerInfo->headerLength > 0) {
        demuxer->seekPos(headerInfo->headerLength);
        invokeSeekFunc(Range(headerInfo->headerLength, 0));
        clearData();
        LogI("demuxer seek to:{}", headerInfo->headerLength);
    }

    DemuxerResult result;
    result.resultCode = DemuxerResultCode::ParsedHeader;
    invokeHandleResultFunc(std::move(result));

    probeBuffer_.reset();

#ifdef SLARK_PERF_ENABLED
    auto session = session_.lock();
    SLARK_TRACE_MILESTONE(session.get(), "DemuxerOpened");
#endif

#if DEBUG
    if (demuxer->type() == DemuxerType::MP4) {
        if (auto mp4 = std::dynamic_pointer_cast<Mp4Demuxer>(demuxer)) {
            LogI("mp4 info:{}", mp4->description());
        }
    }
#endif
    return true;
}

bool DemuxerComponent::openDemuxer(DataPacket& packet) noexcept {
    if (probeBuffer_ == nullptr) {
        probeBuffer_ = std::make_unique<Buffer>();
    }
    if (!probeBuffer_->append(static_cast<uint64_t>(packet.offset), std::move(packet.data))) {
        LogE("append data error:{}", packet.offset);
        return false;
    }
    if (!demuxer_.isValid() && !createDemuxer()) {
        LogE("create demuxer failed.");
        return false;
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
    }
    demuxer->init(config_);
    // 拷贝而非 move，保证 reset 后可复用
    demuxer_.reset(demuxer);
    return true;
}

void DemuxerComponent::seekToPos(uint64_t pos) noexcept {
    if (isClosed_) {
        LogE("demuxer component is closed.");
        return;
    }
    if (auto demuxer = demuxer_.load()) {
        demuxer->seekPos(pos);
    }
}

uint64_t DemuxerComponent::getSeekToPos(double time) noexcept {
    auto demuxer = demuxer_.load();
    if (!demuxer) {
        LogE("demuxer is nullptr.");
        return 0;
    }
    if (!demuxer->isOpened()) {
        LogE("demuxer is not opened.");
        return 0;
    }
    return demuxer->getSeekToPos(time);
}

} // namespace slark
