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

    void release(TexturePtr texture);

    [[nodiscard]] size_t size() const {
        return pool_.size();
    }

    [[nodiscard]] size_t maxSize() const {
        return maxSize_;
    }

    [[nodiscard]] bool isFull() const {
        return pool_.size() >= maxSize_;
    }

    [[nodiscard]] bool isEmpty() const {
        return pool_.empty();
    }

    [[nodiscard]] bool empty() const {
        return pool_.empty();
    }

    void clear() {
        pool_.clear();
    }

private:
    size_t maxSize_;
    std::deque<TexturePtr> pool_;
};

} // namespace slark
