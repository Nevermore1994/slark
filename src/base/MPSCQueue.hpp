#pragma once
#include <atomic>
#include <concepts>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace slark {

template <typename T>
    requires(std::movable<T> or std::copyable<T>)
class MPSCQueue {
private:
    struct Node {
        std::atomic<Node*> next{nullptr};
        T value;
        Node() = default;

        template <typename U>
            requires std::copyable<U> && std::convertible_to<U, T>
        Node(const U& val) : value(val) {}

        template <typename U>
            requires std::movable<U> && std::convertible_to<U, T>
        Node(U&& val) : value(std::move(val)) {}
    };

    alignas(64) std::atomic<Node*> tail_;
    Node* head_;
    std::atomic<uint64_t> size_{0};

public:
    MPSCQueue() {
        Node* dummy = new Node();
        head_ = dummy;
        tail_.store(dummy, std::memory_order_relaxed);
    }

    ~MPSCQueue() {
        // 逐节点释放，不要求 T 默认可构造
        Node* cur = head_->next.load(std::memory_order_acquire);
        delete head_;
        while (cur) {
            Node* next = cur->next.load(std::memory_order_relaxed);
            delete cur;
            cur = next;
        }
    }

    template <typename U>
    void push(const U& value)
        requires(std::copyable<U> && std::convertible_to<U, T>)
    {
        Node* node = new Node(value);
        Node* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
        size_.fetch_add(1, std::memory_order_relaxed);
    }

    template <typename U>
    void push(U&& value)
        requires(std::convertible_to<U, T> || std::is_same_v<std::remove_cvref<U>, T>)
    {
        Node* node = new Node(std::move(value));
        Node* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
        size_.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] bool tryPop(T& value) {
        Node* next = head_->next.load(std::memory_order_acquire);
        if (!next) {
            return false;
        }
        value = std::move(next->value);
        Node* oldHead = head_;
        head_ = next;
        delete oldHead;
        size_.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }

    [[nodiscard]] std::vector<T> tryPopAll() {
        std::vector<T> result;
        T value;
        while (tryPop(value)) {
            result.push_back(std::move(value));
        }
        return result;
    }

    [[nodiscard]] uint64_t size() const noexcept {
        return size_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] bool empty() const {
        return head_->next.load(std::memory_order_acquire) == nullptr;
    }
};

} // namespace slark