//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//
#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "IDemuxer.h"
#include "IStream.h"
#include "StreamSource.h"

namespace slark::engine::fake {

// A stream the test drives by hand: it never delivers anything on its own.
class ScriptedStream final : public IStream {
 public:
    struct SeekCall {
        uint64_t pos;
        uint64_t generation;
    };

    auto open(const OpenParams& params) -> StreamResult<StreamInfo> override {
        params_ = params;
        opened.store(true, std::memory_order_release);
        StreamInfo info;
        info.isSeekable = true;
        return info;
    }

    [[nodiscard]] const OpenParams& openedParams() const noexcept {
        return params_;
    }

    void close() noexcept override {
        if (closeCounter) {
            ++*closeCounter;
        }
    }

    void start() noexcept override {
        ++starts;
    }

    void pause() noexcept override {}

    auto seek(uint64_t position, uint64_t generation) -> StreamResult<void> override {
        std::lock_guard lock(mutex);
        seeks.push_back({position, generation});
        return {};
    }

    void setReadRange(Range) noexcept override {}

    void setProductionGate(std::function<bool()> gate) noexcept override {
        gate_ = std::move(gate);
    }

    void openFetchGate() noexcept override {
        ++gateOpens;
    }

    [[nodiscard]] StreamState state() const noexcept override {
        return StreamState::Reading;
    }

    [[nodiscard]] StreamInfo info() const noexcept override {
        return {};
    }

    [[nodiscard]] uint64_t position() const noexcept override {
        return 0;
    }

    [[nodiscard]] bool isCompleted() const noexcept override {
        return false;
    }

    void deliver(uint64_t offset, uint64_t generation, size_t size = 16) {
        std::string bytes(size, 'x');
        auto owner = std::make_shared<Data>(std::string_view(bytes));
        params_.onData(DataChunk{.data = DataView(owner->view()),
                                 .owner = owner,
                                 .offset = offset,
                                 .sourceGeneration = generation});
    }

    void complete(uint64_t generation) {
        params_.onComplete(generation);
    }

    [[nodiscard]] bool gateIsOpen() const {
        return !gate_ || gate_();
    }

    [[nodiscard]] std::vector<SeekCall> seekCalls() {
        std::lock_guard lock(mutex);
        return seeks;
    }

    // Set once open() has stored the callbacks; deliver() is only valid after that.
    std::atomic<bool> opened{false};
    std::atomic<int> starts{0};
    std::atomic<int> gateOpens{0};
    std::atomic<int>* closeCounter = nullptr;

 private:
    OpenParams params_;
    std::function<bool()> gate_;
    std::mutex mutex;
    std::vector<SeekCall> seeks;
};

// Produces whatever the test queued, one batch per parseData call. `open` succeeds immediately,
// or after asking for a reposition when `needsReposition` is set.
class ScriptedDemuxer final : public IDemuxer {
 public:
    struct Frame {
        AVFrameType type = AVFrameType::Video;
        double pts = 0;
        bool key = false;
    };

    ScriptedDemuxer() {
        audioInfo_ = std::make_shared<AudioInfo>();
        audioInfo_->sampleRate = 48000;
        audioInfo_->channels = 2;
        videoInfo_ = std::make_shared<VideoInfo>();
        totalDuration_ = CTime(60.0);
    }

    bool open(std::unique_ptr<Buffer>&) noexcept override {
        if (repositionTo && !repositioned) {
            repositioned = true;
            pendingSeekPos_ = *repositionTo;
            return false;
        }
        isOpened_ = true;
        if (headerLength > 0) {
            headerInfo_ = std::make_shared<DemuxerHeaderInfo>();
            headerInfo_->headerLength = headerLength;
        }
        return true;
    }

    void close() noexcept override {}

    DemuxerResult parseData(DataPacket& packet) noexcept override {
        {
            std::unique_lock gateLock(blockMutex);
            blockCv.wait(gateLock, [this] {
                return !blocked_;
            });
        }
        ++parsed;
        DemuxerResult result;
        std::lock_guard lock(mutex);
        const auto it = batches.find(packet.offset);
        if (it == batches.end()) {
            return result;
        }
        for (const auto& f : it->second) {
            auto frame = std::make_unique<AVFrame>(f.type);
            frame->pts = static_cast<int64_t>(f.pts * 1000);
            frame->timeScale = 1000;
            if (f.type == AVFrameType::Video) {
                auto info = std::make_shared<VideoFrameInfo>();
                info->frameType = f.key ? VideoFrameType::IFrame : VideoFrameType::PFrame;
                info->isIDRFrame = f.key;
                frame->info = info;
                result.videoFrames.push_back(std::move(frame));
            } else {
                frame->info = std::make_shared<AudioFrameInfo>();
                result.audioFrames.push_back(std::move(frame));
            }
        }
        batches.erase(it);
        return result;
    }

    [[nodiscard]] uint64_t getSeekToPos(double time) noexcept override {
        lastSeekTime = time;
        return seekPos_;
    }

    // What parseData returns for the chunk delivered at `offset`.
    void queue(uint64_t offset, std::vector<Frame> frames) {
        std::lock_guard lock(mutex);
        batches[offset] = std::move(frames);
    }

    // While blocked, parseData waits: the loader thread is stuck behind the stream.
    void setBlocked(bool blocked) {
        {
            std::lock_guard lock(blockMutex);
            blocked_ = blocked;
        }
        blockCv.notify_all();
    }

    std::optional<uint64_t> repositionTo;
    bool repositioned = false;
    uint64_t headerLength = 0;
    uint64_t seekPos_ = 1000;
    std::atomic<double> lastSeekTime{-1};
    std::atomic<int> parsed{0};

 private:
    std::mutex mutex;
    std::map<uint64_t, std::vector<Frame>> batches;
    std::mutex blockMutex;
    std::condition_variable blockCv;
    bool blocked_ = false;
};

// A StreamSource over the two scripted parts. Raw pointers stay valid while the source lives.
class ScriptedSource final : public StreamSource {
 public:
    static std::unique_ptr<ScriptedSource> create(SourceOptions options, SeekEpoch generation, Wake wake) {
        std::shared_ptr<ScriptedDemuxer> demuxer;
        if (!options.demuxerFactory) {
            demuxer = std::make_shared<ScriptedDemuxer>();
            options.demuxerFactory = [demuxer] {
                return demuxer;
            };
        }
        std::unique_ptr<ScriptedSource> source(
            new ScriptedSource(std::move(options), generation, std::move(wake)));
        source->demuxer = demuxer.get();
        source->start();
        return source;
    }

    // A test that fails while the demuxer is blocked must not turn into a hang: release it
    // first, because close() waits for the loader and the loader would be stuck in parseData.
    ~ScriptedSource() override {
        if (demuxer) {
            demuxer->setBlocked(false);
        }
        close();
    }

    // Valid to use once ready() is true.
    [[nodiscard]] bool ready() const {
        auto* s = streamPtr_.load(std::memory_order_acquire);
        return s && s->opened.load(std::memory_order_acquire);
    }

    [[nodiscard]] ScriptedStream* openedStream() const {
        return streamPtr_.load(std::memory_order_acquire);
    }

    ScriptedStream* stream = nullptr;
    ScriptedDemuxer* demuxer = nullptr;
    // Mirrors the HTTP source (false) or the file source (true).
    std::atomic<bool> pulls{true};
    std::atomic<int> streamCloses{0};
    std::atomic<ScriptedStream*> streamPtr_{nullptr};
    std::atomic<int> gateClosedCalls{0};
    std::atomic<int> gateOpenCalls{0};

 private:
    using StreamSource::StreamSource;

    StreamResult<std::shared_ptr<IStream>> createStream() override {
        auto s = std::make_shared<ScriptedStream>();
        s->closeCounter = &streamCloses;
        stream = s.get();
        streamPtr_.store(s.get(), std::memory_order_release);
        return std::shared_ptr<IStream>(s);
    }

    StreamResult<void> seekStream(Time::TimePoint, uint64_t pos, SeekEpoch generation) override {
        return stream_->seek(pos, generation);
    }

    [[nodiscard]] bool pullsGate() const override {
        return pulls;
    }

    void applyFlowGate(bool closed) override {
        (closed ? gateClosedCalls : gateOpenCalls)++;
    }
};

}  // namespace slark::engine::fake
