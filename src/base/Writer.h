//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once
#include "File.h"
#include "IOBase.h"
#include "MPSCQueue.hpp"
#include "Synchronized.hpp"
#include "Thread.h"

namespace slark {

class Writer : public NonCopyMovable {
public:
    Writer();
    ~Writer() override;

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
    bool checkState() noexcept;
    void process() noexcept;
    void closeFile() noexcept;

    std::atomic_bool isOpen_ = false;
    std::atomic_bool isStop_ = false;
    MPSCQueue<DataPtr> dataQueue_;
    Synchronized<std::unique_ptr<File::WriteFile>> file_;
    std::shared_ptr<Thread> worker_;
};

} // namespace slark
namespace slark
