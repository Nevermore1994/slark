//
// Created by Nevermore- on 2025/5/8.
//

#include "FrameBufferPool.h"
#include "Log.hpp"

namespace slark {

std::string generateKey(uint32_t width, uint32_t height, std::thread::id id) {
    static std::hash<std::thread::id> threadIdHash;
    return std::format("{}_{}_{}", threadIdHash(id), width, height);
}

FrameBufferRefPtr FrameBufferPool::fetch(uint32_t width, uint32_t height) noexcept {
    auto key = generateKey(width, height, std::this_thread::get_id());
    auto it = frameBufferPool_.find(key);
    if (it != frameBufferPool_.end()) {
        std::erase_if(it->second.useList, [&](auto& frameBuffer) {
            if (frameBuffer.use_count() == 1 && frameBuffer->texture().use_count() == 1) {
                it->second.freeList.push_back(frameBuffer);
                return true;
            }
            return false;
        });
        if (!it->second.freeList.empty()) {
            auto frameBuffer = it->second.freeList.front();
            it->second.freeList.pop_front();
            it->second.useList.push_back(frameBuffer);
            return frameBuffer;
        }
    } else {
        frameBufferPool_.try_emplace(key, maxFrameBufferCount_);
    }
    auto& node = frameBufferPool_.at(key);
    auto frameBuffer = std::make_shared<FrameBuffer>();
    if (frameBuffer->init(width, height)) {
        node.useList.push_back(frameBuffer);
    } else {
        LogE("Failed to create framebuffer");
    }
    if (!frameBuffer->isValid()) {
        frameBuffer.reset();
    }

    while (node.isFull()) {
        if (node.freeList.empty()) {
            break;
        }
        node.freeList.pop_back();
    }
    return frameBuffer;
}
} // slark