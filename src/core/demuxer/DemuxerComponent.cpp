//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DemuxerComponent.h"

#include "Log.hpp"
#include "Mp4Demuxer.h"

#ifdef SLARK_PERF_ENABLED
#include "Perf.h"
#endif

namespace slark {

DemuxerComponent::DemuxerComponent(ReceiverPtr<DataPacket> receiver, DemuxerConfig config)
    : receiver_(std::move(receiver)), config_(std::move(config)) {}

DemuxerComponent::~DemuxerComponent() {
    stop();
}

void DemuxerComponent::stop() noexcept {
    LogI("[DemuxerComponent::stop] stopping");
    if (receiver_) {
        receiver_->wakeup();
    }
    if (worker_) {
        worker_->stop();
        worker_->join();  // 同步等 worker 退出，避免 loop 仍在跑而成员被 reset 导致 UAF
        ThreadManager::shareInstance().remove(worker_);
        worker_.reset();
    }
    reset();
    isClosed_ = true;
}

void DemuxerComponent::reset() noexcept {
    if (auto demuxer = demuxer_.load()) {
        demuxer->close();
    }
    flushed_ = false;
    demuxer_.reset();
}

void DemuxerComponent::close() noexcept {
    reset();
    isClosed_ = true;
}

void DemuxerComponent::start() noexcept {
    if (worker_) {
        worker_->start();
        return;
    }
    LogI("[DemuxerComponent::start] starting DemuxerWorker");
    worker_ = Thread::createWithLoop("DemuxerWorker", [this](Thread& thread) {
        runLoop(thread);
    });
    ThreadManager::shareInstance().add(worker_);
}

void DemuxerComponent::flush() noexcept {
    flushed_.store(true, std::memory_order_release);
    if (receiver_) {
        (void)receiver_->tryReceiveAll();
    }
    if (auto demuxer = demuxer_.load()) {
        demuxer->flush();
    }
    if (receiver_) {
        receiver_->wakeup();
    }
}

void DemuxerComponent::pause() noexcept {
    if (worker_) {
        worker_->pause();
    }
    if (receiver_) {
        receiver_->wakeup();
    }
}

void DemuxerComponent::resume() noexcept {
    if (worker_) {
        worker_->start();
    }
}

void DemuxerComponent::runLoop(Thread& thread) noexcept {
    while (!thread.shouldStop()) {
        thread.drainPendingWork();

        if (flushed_.exchange(false, std::memory_order_acq_rel)) {
            (void)receiver_->tryReceiveAll();
        }

        auto exp = receiver_->receive();

        if (!exp.has_value()) {
            if (exp.error() == ChannelEventType::Awakened) {
                continue;
            }
            LogI("[DemuxerComponent::runLoop] receiver closed, exiting loop");
            break;
        }
        DataPacket packet = std::move(*exp);
        LogI("demuxer worker: received data packet offset={}, size={}", packet.offset,
             packet.data ? packet.data->size() : 0);

        if (flushed_.load(std::memory_order_acquire)) {
            continue;
        }

        auto expectedOffset = pendingSeekVerifiedOffset_.load(std::memory_order_acquire);
        if (expectedOffset != 0) {
            if (static_cast<uint64_t>(packet.offset) != expectedOffset) {
                LogW("open: discard stale packet offset={}, expected={}", packet.offset,
                     expectedOffset);
                continue;
            }
            pendingSeekVerifiedOffset_.store(0, std::memory_order_release);
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
        if (result.resultCode != DemuxerResultCode::Normal) {
            LogI("[DemuxerComponent::runLoop] parseData: resultCode={} videoFrames={} "
                 "audioFrames={} offset={}",
                 static_cast<int>(result.resultCode), result.videoFrames.size(),
                 result.audioFrames.size(), packet.offset);
        }
        invokeHandleResultFunc(std::move(result));
    }
    LogI("[DemuxerComponent::runLoop] loop exited");
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
        if (auto seekTo = demuxer->pendingSeekPos(); seekTo.has_value()) {
            demuxer->resetPendingSeek();
            probeBuffer_.reset();
            invokeSeekFunc(Range(*seekTo));
            pendingSeekVerifiedOffset_.store(*seekTo, std::memory_order_release);
            flushed_.store(false, std::memory_order_release);
        }
        return false;
    }

    LogI("[DemuxerComponent::open] demuxer opened: type={} probeRemain={}",
         static_cast<int32_t>(demuxer->type()), probeBuffer_ ? probeBuffer_->length() : 0);

    if (auto headerInfo = demuxer->headerInfo(); headerInfo && headerInfo->headerLength > 0) {
        demuxer->seekPos(headerInfo->headerLength);
        invokeSeekFunc(Range(headerInfo->headerLength));
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
        LogW("append failed, offset={}, skip this packet", packet.offset);
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
    LogI("[DemuxerComponent::createDemuxer] created: type={} probeSize={}",
         static_cast<int32_t>(demuxerType), dataView.size());
    return true;
}

void DemuxerComponent::seekToPos(uint64_t pos) noexcept {
    if (isClosed_) {
        LogE("demuxer component is closed.");
        return;
    }
    auto demuxer = demuxer_.load();
    if (!demuxer) {
        return;
    }
    demuxer->seekPos(pos);
    // HLS 的 pos 是 segment index，不能作为 byte offset 通知 IO 层
    if (demuxer->type() != DemuxerType::HLS) {
        // IO 正处在 pause 状态，排空 flush→seekToPos 窗口期 IO 可能已写入的陈旧数据
        if (receiver_) {
            (void)receiver_->tryReceiveAll();
        }
        // pendingSeekVerifiedOffset_ 兜底过滤任何遗漏的过期包
        pendingSeekVerifiedOffset_.store(pos, std::memory_order_release);
        // 同步 IO seek → IO 从新位置发送的数据 offset 需匹配 expectedOffset
        invokeSeekFunc(Range(pos));
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

void DemuxerComponent::seekToTime(double time) noexcept {
    postToWorker([this, time]() {
        if (isClosed_) {
            return;
        }
        auto demuxer = demuxer_.load();
        if (!demuxer || !demuxer->isOpened()) {
            return;
        }
        auto pos = demuxer->getSeekToPos(time);
        if (pos > 0) {
            seekToPos(pos);
        }
    });
}

void DemuxerComponent::drainWorkerTasks() noexcept {
    if (worker_) {
        worker_->drainPendingWork();
    }
}

void DemuxerComponent::postToWorker(ThreadTask task) noexcept {
    if (worker_) {
        worker_->postTask(std::move(task));
    }
}

}  // namespace slark
