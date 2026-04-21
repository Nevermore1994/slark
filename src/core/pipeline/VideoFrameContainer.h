//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

#pragma once

#include "AVFrame.hpp"

#include <deque>
#include <map>
#include <optional>
#include <variant>

namespace slark {

using OrderedFrameQueue = std::deque<TaggedFrame>;
using SortedFrameMap = std::map<int64_t, TaggedFrame>;

/**
 * @brief 视频帧容器，运行时按是否存在 B 帧选择底层数据结构。
 *
 * 默认使用 deque（无 B 帧，FIFO），首次检测到 B 帧后调用 switchToSorted() 切换为 map（按 pts
 * 排序），切换为单向操作，不支持回退。同一流中 pts 唯一，无需 multimap。
 */
class VideoFrameContainer {
public:
    VideoFrameContainer() : storage_(OrderedFrameQueue{}) {}

    void switchToSorted() noexcept {
        if (isSorted()) {
            return;
        }
        SortedFrameMap sorted;
        auto& q = std::get<OrderedFrameQueue>(storage_);
        for (auto& tf : q) {
            auto pts = tf.frame ? tf.frame->pts : 0;
            sorted.emplace(pts, std::move(tf));
        }
        storage_ = std::move(sorted);
    }

    void push(TaggedFrame tf) noexcept {
        std::visit(
            [&tf](auto& s) {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, OrderedFrameQueue>) {
                    s.emplace_back(std::move(tf));
                } else {
                    auto pts = tf.frame ? tf.frame->pts : 0;
                    s.emplace(pts, std::move(tf));
                }
            },
            storage_);
    }

    [[nodiscard]] AVFrameRefPtr peek(uint64_t currentEpoch) const noexcept {
        return std::visit(
            [currentEpoch](const auto& s) -> AVFrameRefPtr {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, OrderedFrameQueue>) {
                    for (const auto& tf : s) {
                        if (tf.seekEpoch == currentEpoch) {
                            return tf.frame;
                        }
                    }
                    return nullptr;
                } else {
                    for (const auto& [pts, tf] : s) {
                        if (tf.seekEpoch == currentEpoch) {
                            return tf.frame;
                        }
                    }
                    return nullptr;
                }
            },
            storage_);
    }

    [[nodiscard]] AVFrameRefPtr pop(uint64_t currentEpoch) noexcept {
        return std::visit(
            [currentEpoch](auto& s) -> AVFrameRefPtr {
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, OrderedFrameQueue>) {
                    while (!s.empty() && s.front().seekEpoch != currentEpoch) {
                        s.pop_front();
                    }
                    if (s.empty()) {
                        return nullptr;
                    }
                    auto frame = std::move(s.front().frame);
                    s.pop_front();
                    return frame;
                } else {
                    while (!s.empty() && s.begin()->second.seekEpoch != currentEpoch) {
                        s.erase(s.begin());
                    }
                    if (s.empty()) {
                        return nullptr;
                    }
                    auto frame = std::move(s.begin()->second.frame);
                    s.erase(s.begin());
                    return frame;
                }
            },
            storage_);
    }

    void popFront() noexcept {
        std::visit(
            [](auto& s) {
                if (!s.empty()) {
                    using T = std::decay_t<decltype(s)>;
                    if constexpr (std::is_same_v<T, OrderedFrameQueue>) {
                        s.pop_front();
                    } else {
                        s.erase(s.begin());
                    }
                }
            },
            storage_);
    }

    void clear() noexcept {
        std::visit(
            [](auto& s) {
                s.clear();
            },
            storage_);
    }

    [[nodiscard]] bool empty() const noexcept {
        return std::visit(
            [](const auto& s) {
                return s.empty();
            },
            storage_);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return std::visit(
            [](const auto& s) {
                return s.size();
            },
            storage_);
    }

    [[nodiscard]] std::optional<double> backPts() const noexcept {
        return std::visit(
            [](const auto& s) -> std::optional<double> {
                if (s.empty()) {
                    return std::nullopt;
                }
                using T = std::decay_t<decltype(s)>;
                if constexpr (std::is_same_v<T, OrderedFrameQueue>) {
                    const auto& tf = s.back();
                    return tf.frame ? std::optional<double>(tf.frame->ptsTime()) : std::nullopt;
                } else {
                    const auto& [pts, tf] = *s.rbegin();
                    return tf.frame ? std::optional<double>(tf.frame->ptsTime()) : std::nullopt;
                }
            },
            storage_);
    }

private:
    [[nodiscard]] bool isSorted() const noexcept {
        return std::holds_alternative<SortedFrameMap>(storage_);
    }

    std::variant<OrderedFrameQueue, SortedFrameMap> storage_;
};

} // namespace slark
namespace slark
