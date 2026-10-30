//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <variant>

#include "File.h"
#include "IOBase.h"
#include "Synchronized.hpp"
#include "TaskThread.h"

namespace slark {

class Writer : public NonCopyMovable {
 public:
    static constexpr size_t kMaxParkedBytes = 4 * 1024 * 1024;

    Writer();
    ~Writer();

    bool open(std::string_view path, bool isAppend = false) noexcept;
    void close() noexcept;
    void dispose() noexcept;

    [[nodiscard]] std::string path() noexcept;
    [[nodiscard]] IOState state() noexcept;

    bool write(DataPtr data) noexcept;
    bool write(const Data& data) noexcept;
    bool write(std::string_view str) noexcept;

    [[nodiscard]] uint32_t writeCount() noexcept;
    [[nodiscard]] uint64_t writeSize() noexcept;
    [[nodiscard]] uint32_t getHashId() noexcept;

    [[nodiscard]] bool isOpen() const noexcept {
        return isOpen_.load(std::memory_order_acquire);
    }

 private:
    struct Completion {
        bool wait() noexcept;
        void complete(bool result) noexcept;

        std::mutex mutex;
        std::condition_variable cond;
        bool done = false;
        bool result = false;
    };

    struct DataItem {
        DataPtr data;
    };

    struct OpenItem {
        std::string path;
        bool append;
        std::shared_ptr<Completion> completion;
    };

    struct CloseItem {
        std::shared_ptr<Completion> completion;
    };

    struct StopItem {
        std::shared_ptr<Completion> completion;
    };

    using Item = std::variant<DataItem, OpenItem, CloseItem, StopItem>;

    bool enqueue(Item item, bool stopping = false) noexcept;
    void drain() noexcept;
    void process(Item item) noexcept;
    bool openFile(const std::string& path, bool append) noexcept;
    void closeFile() noexcept;
    void writeData(DataPtr data) noexcept;
    void flushParked() noexcept;
    void park(DataPtr data) noexcept;

    std::atomic_bool isOpen_{false};
    mutable std::mutex mutex_;
    std::deque<Item> inbox_;
    bool scheduled_ = false;
    bool disposed_ = false;
    bool fileOpen_ = false;
    std::deque<DataPtr> parked_;
    size_t parkedBytes_ = 0;
    Synchronized<std::unique_ptr<File::WriteFile>> file_;
    std::shared_ptr<TaskThread> worker_;
};

}  // namespace slark
