//
// Created by Nevermore- on 2025/5/15.
//


#include <deque>
#include <mutex>
#include "Texture.h"

namespace slark {

class TexturePool : public std::enable_shared_from_this<TexturePool> {
public:
    explicit TexturePool(size_t maxSize = 8)
        : maxSize_(maxSize) {}

    TexturePtr acquire(
        uint32_t width,
        uint32_t height,
        TextureConfig config = {}
    );

    void restore(TexturePtr texture);

    [[nodiscard]] size_t size() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size();
    }

    [[nodiscard]] size_t maxSize() const noexcept {
        return maxSize_;
    }

    [[nodiscard]] bool isFull() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size() >= maxSize_;
    }

    [[nodiscard]] bool empty() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.empty();
    }

    void clear() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.clear();
    }

private:
    mutable std::mutex mutex_;
    size_t maxSize_;
    std::deque<TexturePtr> pool_;
};

} // namespace slark
