//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "File.h"
#include "IStream.h"

namespace slark {

class FileStream : public IStream {
 public:
    FileStream();
    ~FileStream() override;

    StreamResult<StreamInfo> open(const OpenParams& params) override;
    void close() noexcept override;
    void start() noexcept override;
    void pause() noexcept override;
    StreamResult<void> seek(uint64_t position) override;
    void updateRange(Range range) noexcept override;

    [[nodiscard]] StreamState state() const noexcept override {
        return state_.load();
    }

    [[nodiscard]] const StreamInfo& info() const noexcept override {
        return info_;
    }

    [[nodiscard]] uint64_t position() const noexcept override {
        return position_.load();
    }

    [[nodiscard]] bool isRunning() const noexcept override {
        return isRunning_.load();
    }

    [[nodiscard]] bool isCompleted() const noexcept override {
        return isCompleted_.load();
    }

 private:
    void readLoop() noexcept;
    void notifyThread() noexcept;
    bool deliverData(std::shared_ptr<Data> buffer, bool eof) noexcept;

 private:
    std::unique_ptr<File::ReadFile> file_;
    std::unique_ptr<std::thread> thread_;
    std::mutex mutex_;
    std::condition_variable cv_;

    OpenParams params_;
    Range readRange_;
    uint64_t fileSize_ = 0;

    StreamInfo info_;

    std::atomic<StreamState> state_{StreamState::Idle};
    std::atomic<uint64_t> position_{0};
    std::atomic<bool> isRunning_{false};
    std::atomic<bool> isCompleted_{false};
    std::atomic<bool> shouldStop_{false};
    std::atomic<bool> shouldPause_{false};
    std::atomic<bool> pendingSeekFlag_{false};
    std::atomic<bool> eofReached_{false};
    uint64_t pendingSeekPos_ = 0;
    bool seekCompleted_ = false;
};

}  // namespace slark
