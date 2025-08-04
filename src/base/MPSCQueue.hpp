#pragma once
#include <atomic>
#include <concepts>
#include <memory>
#include <thread>
#include <utility>

template <typename T>
    requires (std::movable<T> or std::copyable<T>)
class MPSCQueue {
private:
    struct Node {
        std::atomic<Node*> next{nullptr};
        T value;
        Node() = default;
        
        template<typename U> requires std::copyable<U> && std::convertible_to<U, T>
        Node(const U& val) : value(val) {
        
        }
        template<typename U> requires std::movable<U> && std::convertible_to<U, T>
        Node(U&& val) : value(std::move(val)) {}
    };

    alignas(64) std::atomic<Node*> tail_; // producers push here
    Node* head_;                          // consumer pops from here

public:
    MPSCQueue() {
        Node* dummy = new Node();
        head_ = dummy;
        tail_.store(dummy, std::memory_order_relaxed);
    }

    ~MPSCQueue() {
        T tmp;
        while (tryPop(tmp)); // drain
        delete head_; // delete dummy node
    }

    template <typename U>
    void push(const U& value) requires (std::copyable<U> && std::convertible_to<U, T>) {
        Node* node = new Node(value);
        Node* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
    }

    template <typename U>
    void push(U&& value) requires (std::convertible_to<U, T> || std::is_same_v<std::remove_cvref<U>, T>) {
        Node* node = new Node(std::move(value));
        Node* prev = tail_.exchange(node, std::memory_order_acq_rel);
        prev->next.store(node, std::memory_order_release);
    }
    
    [[nodiscard]] bool tryPop(T& value) {
        Node* next = head_->next.load(std::memory_order_acquire);
        if (!next) return false;
        value = std::move(next->value);
        Node* oldHead = head_;
        head_ = next;
        delete oldHead;
        return true;
    }

    [[nodiscard]] bool empty() const {
        return head_->next.load(std::memory_order_acquire) == nullptr;
    }
};