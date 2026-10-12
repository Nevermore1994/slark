//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#include "DemuxerComponent.h"

#include <cstdlib>

#include "Log.hpp"
#include "Mp4Demuxer.h"

#ifdef SLARK_PERF_ENABLED
#include "Perf.h"
#endif

namespace slark {

DemuxerComponent::DemuxerComponent(ReceiverPtr<DataPacket> receiver, DemuxerConfig config)
    : receiver_(std::move(receiver)), config_(std::move(config)) {}

DemuxerComponent::~DemuxerComponent() {
    auto worker = worker_.load();
    if (worker && worker->isCurrentThread()) {
        LogP("DemuxerComponent destroyed on its worker thread");
        std::abort();
    }
    stop();
}

void DemuxerComponent::stop() noexcept {
    std::shared_ptr<LoopThread> worker;
    {
        std::lock_guard lock(startMutex_);
        stopped_.store(true, std::memory_order_release);
        isClosed_.store(true, std::memory_order_release);
        worker = worker_.load();
    }
    if (worker && worker->isCurrentThread()) {
        worker->stop();
        return;
    }
    if (worker) {
        worker->stop();
        worker->join();
    }
    std::call_once(finalizeOnce_, [this] { reset(); });
    if (worker) {
        ThreadManager::shareInstance().remove(worker);
    }
}

void DemuxerComponent::reset() noexcept {
    if (auto demuxer = demuxer_.load()) {
        demuxer->close();
    }
    flushed_ = false;
    pendingSeekVerification_.store(false, std::memory_order_release);
    pendingSeekVerifiedOffset_.store(0, std::memory_order_release);
    sourceGeneration_.store(0, std::memory_order_release);
    probeBuffer_.reset();
    demuxer_.reset();
    header_.reset();
}

void DemuxerComponent::close() noexcept {
    stop();
}

void DemuxerComponent::start() noexcept {
    std::lock_guard lock(startMutex_);
    if (stopped_.load(std::memory_order_acquire)) {
        LogW("DemuxerComponent::start ignored, component already stopped");
        return;
    }
    if (worker_.load()) {
        return;
    }
    auto receiver = receiver_.get();
    auto worker = LoopThread::create(
        "DemuxerWorker", [this](LoopThread& thread) { runLoop(thread); },
        [receiver] {
            if (receiver) {
                receiver->wakeup();
            }
        });
    if (!worker) {
        LogE("create DemuxerWorker failed");
        return;
    }
    worker_.reset(worker);
    ThreadManager::shareInstance().add(worker);
}

void DemuxerComponent::flush() noexcept {
    flushed_.store(true, std::memory_order_release);
    if (receiver_) {
        (void)receiver_->tryReceiveAll();
    }
    runInDomain([this] {
        if (auto demuxer = demuxer_.load()) {
            demuxer->flush();
        }
    });
    if (receiver_) {
        receiver_->wakeup();
    }
}

void DemuxerComponent::pause() noexcept {
    paused_.store(true, std::memory_order_release);
    if (auto worker = worker_.load()) {
        worker->notify();
    }
    if (receiver_) {
        // Wake receive() so the loop observes paused_ instead of parsing a post-pause packet.
        receiver_->wakeup();
    }
}

void DemuxerComponent::resume() noexcept {
    paused_.store(false, std::memory_order_release);
    if (auto worker = worker_.load()) {
        worker->notify();
    }
    if (receiver_) {
        receiver_->wakeup();
    }
}

void DemuxerComponent::runLoop(LoopThread& thread) noexcept {
    while (!thread.isStopRequested()) {
        thread.drainPendingWork();

        if (paused_.load(std::memory_order_acquire)) {
            thread.waitForWork([this] { return !paused_.load(std::memory_order_acquire); });
            continue;
        }

        // Consume the flush marker so packets queued after flush() are not discarded below.
        // Draining the channel here would drop data that already belongs to the new operation;
        // stale packets are filtered by source generation instead.
        (void)flushed_.exchange(false, std::memory_order_acq_rel);

        auto exp = receiver_->receive();

        if (!exp.has_value()) {
            if (exp.error() == ChannelEventType::Awakened) {
                continue;
            }
            LogI("[DemuxerComponent::runLoop] receiver closed, exiting loop");
            break;
        }
        DataPacket packet = std::move(*exp);
        const auto activeGeneration = sourceGeneration_.load(std::memory_order_acquire);
        if (packet.sourceGeneration < activeGeneration) {
            LogW("discard stale source packet generation={}, active={}",
                 packet.sourceGeneration, activeGeneration);
            continue;
        }
        if (packet.sourceGeneration > activeGeneration) {
            sourceGeneration_.store(packet.sourceGeneration, std::memory_order_release);
        }
        if (packet.sourceGeneration == 0 && activeGeneration != 0) {
            LogW("discard source packet without generation, active={}", activeGeneration);
            continue;
        }

        if (flushed_.load(std::memory_order_acquire)) {
            continue;
        }

        if (pendingSeekVerification_.load(std::memory_order_acquire)) {
            const auto expectedOffset = pendingSeekVerifiedOffset_.load(std::memory_order_acquire);
            if (packet.isEndOfStream || packet.offset != expectedOffset) {
                LogW("discard stale packet offset={}, expected={}, eos={}", packet.offset,
                     expectedOffset, packet.isEndOfStream);
                continue;
            }
            pendingSeekVerification_.store(false, std::memory_order_release);
        }

        if (packet.isEndOfStream) {
            auto demuxer = demuxer_.load();
            if (demuxer && demuxer->isOpened() && !demuxer->isCompleted()) {
                auto result = demuxer->endOfStream();
                result.sourceGeneration = packet.sourceGeneration;
                invokeHandleResultFunc(std::move(result));
            }
            continue;
        }

        LogI("demuxer worker: received data packet offset={}, size={}", packet.offset,
             packet.data ? packet.data->size() : 0);

        auto demuxer = demuxer_.load();
        if (!demuxer || !demuxer->isOpened()) {
            if (config_.preferredType == DemuxerType::Unknown) {
                openDemuxer(packet);
                continue;
            }
            if (!demuxer && !createDemuxer()) {
                LogE("create preferred demuxer failed.");
                continue;
            }
            if (!open()) {
                LogE("open preferred demuxer failed.");
                continue;
            }
            demuxer = demuxer_.load();
        }

#ifdef SLARK_PERF_ENABLED
        auto session = session_.lock();
        auto t0 = Time::nowTimeStamp().point();
#endif
        auto result = demuxer->parseData(packet);
        result.sourceGeneration = packet.sourceGeneration;
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
    if (config_.preferredType != DemuxerType::Unknown) {
        auto emptyBuffer = std::make_unique<Buffer>();
        return demuxer->open(emptyBuffer);
    }
    if (!demuxer->open(probeBuffer_)) {
        if (auto seekTo = demuxer->pendingSeekPos(); seekTo.has_value()) {
            demuxer->resetPendingSeek();
            probeBuffer_.reset();
            pendingSeekVerifiedOffset_.store(*seekTo, std::memory_order_release);
            pendingSeekVerification_.store(true, std::memory_order_release);
            if (receiver_) {
                (void)receiver_->tryReceiveAll();
            }
            const auto generation = beginSourceOperation();
            invokeSeekFunc(Range(*seekTo), generation);
            flushed_.store(false, std::memory_order_release);
        }
        return false;
    }

    LogI("[DemuxerComponent::open] demuxer opened: type={} probeRemain={}",
         static_cast<int32_t>(demuxer->type()), probeBuffer_ ? probeBuffer_->length() : 0);

    if (auto headerInfo = demuxer->headerInfo(); headerInfo && headerInfo->headerLength > 0) {
        const auto seekPos = headerInfo->headerLength;
        demuxer->seekPos(seekPos);
        pendingSeekVerifiedOffset_.store(seekPos, std::memory_order_release);
        pendingSeekVerification_.store(true, std::memory_order_release);
        if (receiver_) {
            (void)receiver_->tryReceiveAll();
        }
        const auto generation = sourceGeneration_.load(std::memory_order_acquire);
        invokeSeekFunc(Range(seekPos), generation);
        flushed_.store(false, std::memory_order_release);
        LogI("demuxer seek to:{} generation={}", seekPos, generation);
    }

    DemuxerResult result;
    result.resultCode = DemuxerResultCode::ParsedHeader;
    result.sourceGeneration = sourceGeneration_.load(std::memory_order_acquire);
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
    auto demuxerType = config_.preferredType;
    DataView dataView;
    if (demuxerType == DemuxerType::Unknown) {
        if (!probeBuffer_) {
            return false;
        }
        dataView = probeBuffer_->view();
        demuxerType = DemuxerManager::shareInstance().probeDemuxType(dataView);
    }
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
    const auto generation = beginSourceOperation();
    postToWorker([this, pos, generation] {
        seekToPosOnWorker(pos, generation);
    });
}

void DemuxerComponent::seekToPosOnWorker(uint64_t pos, uint64_t generation) noexcept {
    if (isClosed_) {
        LogE("demuxer component is closed.");
        return;
    }
    auto demuxer = demuxer_.load();
    if (!demuxer) {
        return;
    }
    demuxer->seekPos(pos);
    // For HLS, pos is a segment index, not a byte offset that can be passed to the IO layer
    if (demuxer->type() != DemuxerType::HLS) {
        if (receiver_) {
            (void)receiver_->tryReceiveAll();
        }
        pendingSeekVerifiedOffset_.store(pos, std::memory_order_release);
        pendingSeekVerification_.store(true, std::memory_order_release);
        invokeSeekFunc(Range(pos), generation);
        flushed_.store(false, std::memory_order_release);
    }
}

uint64_t DemuxerComponent::getSeekToPos(double time) noexcept {
    uint64_t position = 0;
    runInDomain([this, time, &position] {
        if (auto demuxer = demuxer_.load(); demuxer && demuxer->isOpened()) {
            position = demuxer->getSeekToPos(time);
        }
    });
    return position;
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
            seekToPosOnWorker(pos, beginSourceOperation());
        }
    });
}

bool DemuxerComponent::postToWorker(ThreadTask task) noexcept {
    auto worker = worker_.load();
    if (!worker || stopped_.load(std::memory_order_acquire)) {
        return false;
    }
    return worker->postTask(std::move(task)).has_value();
}

bool DemuxerComponent::runInDomain(ThreadTask task) noexcept {
    auto worker = worker_.load();
    if (!worker || worker->isCurrentThread()) {
        if (task) {
            task();
        }
        return true;
    }
    return worker->postAndWait(std::move(task)).has_value();
}

void DemuxerComponent::publishHeader() noexcept {
    auto header = std::make_shared<MediaHeader>();
    if (auto demuxer = demuxer_.load()) {
        if (auto video = demuxer->videoInfo()) {
            header->video = *video;
        }
        if (auto audio = demuxer->audioInfo()) {
            header->audio = *audio;
        }
        header->duration = demuxer->totalDuration();
    }
    header_.reset(std::move(header));
}

}  // namespace slark
