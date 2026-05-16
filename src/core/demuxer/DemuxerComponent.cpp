//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DemuxerComponent.h"

#include "Mp4Demuxer.h"

#ifdef SLARK_PERF_ENABLED
#include "Perf.h"
#endif

namespace slark {

DemuxerComponent::DemuxerComponent(ReceiverPtr<DataPacket> receiver, DemuxerConfig config)
    : receiver_(std::move(receiver)), config_(std::move(config)) {}

DemuxerComponent::~DemuxerComponent() {
    stopRequested_.store(true, std::memory_order_release);
    if (receiver_) {
        receiver_->wakeup();
    }
    if (worker_) {
        ThreadManager::shareInstance().remove(worker_);
        worker_.reset();
    }
}

void DemuxerComponent::reset() noexcept {
    if (auto demuxer = demuxer_.load()) {
        demuxer->close();
    }
    flushed_ = false;
    demuxer_.reset();
    probeBuffer_.reset();
}

void DemuxerComponent::close() noexcept {
    reset();
    isClosed_ = true;
}

void DemuxerComponent::start() noexcept {
    if (worker_) {
        return;
    }
    worker_ = Thread::createWithRunLoop("DemuxerWorker", [this] {
        runLoop();
    });
    ThreadManager::shareInstance().add(worker_);
}

void DemuxerComponent::flush() noexcept {
    flushed_.store(true, std::memory_order_release);
    // 主调线程先清一次 Channel 残留 packet
    if (receiver_) {
        (void)receiver_->tryReceiveAll();
    }
    if (auto demuxer = demuxer_.load()) {
        demuxer->flush();
    }
    // 唤醒可能在 receive() 阻塞的 worker
    if (receiver_) {
        receiver_->wakeup();
    }
}

void DemuxerComponent::runLoop() noexcept {
    while (!stopRequested_.load(std::memory_order_acquire)) {
        // flush 信号优先处理：清空 Channel 残留数据
        if (flushed_.exchange(false, std::memory_order_acq_rel)) {
            (void)receiver_->tryReceiveAll();
        }

        auto exp = receiver_->receive();

        if (!exp.has_value()) {
            if (exp.error() == ChannelEventType::Awakened) {
                // flush 主动唤醒，回到顶部重新检查 flushed_/stopRequested_
                continue;
            }
            // Closed：上游 sender 已释放，正常退出
            break;
        }
        DataPacket packet = std::move(*exp);

        // flush 期间到来的旧数据丢弃
        if (flushed_.load(std::memory_order_acquire)) {
            continue;
        }

        auto demuxer = demuxer_.load();
        if (!demuxer || !demuxer->isOpened()) {
            openDemuxer(packet);
            continue;
        }

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
        flushed_ = false;
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
