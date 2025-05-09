//
// Created by Nevermore- on 2025/5/8.
//

#pragma once
#include "FrameBuffer.h"
#include "NonCopyable.h"
#include <list>
#include <unordered_map>

namespace slark {
using FrameBufferRefPtrList = std::list<FrameBufferRefPtr>;

constexpr uint8_t kMaxFrameBufferCount = 3;
struct FrameBufferNode {
    FrameBufferRefPtrList useList;
    FrameBufferRefPtrList freeList;

    FrameBufferNode(uint8_t count)
        : capacity(count) {

    }

    bool isFull() const noexcept {
        return (useList.size() + freeList.size()) >= capacity;
    }

    uint8_t capacity = kMaxFrameBufferCount;
};

class FrameBufferPool: public NonCopyable {
public:
    explicit FrameBufferPool(uint8_t maxSlotCount = kMaxFrameBufferCount)
        : maxFrameBufferCount_(maxSlotCount) {

    }

    ~FrameBufferPool() override = default;

    FrameBufferRefPtr fetch(uint32_t width, uint32_t height) noexcept;

private:
    uint8_t maxFrameBufferCount_ = kMaxFrameBufferCount;
    std::unordered_map<std::string, FrameBufferNode> frameBufferPool_;
};

} // slark

