//
// Created by Nevermore on 2023/11/3.
// Slark Channel
// Copyright (c) 2023 Nevermore All rights reserved.
//

#pragma once
#include <condition_variable>
#include <expected>
#include <memory>
#include <tuple>
#include <list>
#include <concepts>
#include <ranges>
#include <type_traits>

namespace slark {

template <typename T>
struct IsSTLStringImpl : public std::false_type {};

template <typename T>
struct IsSTLStringImpl<std::basic_string<T>> : public std::true_type {};

template <typename T>
inline constexpr auto IsSTLString = IsSTLStringImpl<T>::value;

template <typename C>
concept IsRange = std::ranges::viewable_range<C> &&
    (!IsSTLString<C>) &&
    requires(C&& c) {
        std::forward<C>(c).empty();
        typename decltype(c.begin())::value_type;
    };

template <typename I>
    requires requires(I&&) {
        typename std::remove_cvref_t<I>::element_type::ValueType;
    }
struct GetChannelTypeImpl {
    using type = std::remove_cvref_t<I>::element_type::ValueType;
};

template<typename I>
using GetChannelType = typename GetChannelTypeImpl<I>::type;

template <typename T>
class Sender;

template <typename T>
class Receiver;

template <typename T>
struct IsSenderImpl : public std::false_type {};

template <typename T>
struct IsSenderImpl<Sender<T>> : public std::true_type {};

template <typename T>
inline constexpr auto IsSender = IsSenderImpl<T>::value;

template <typename T>
concept IsSenderPtr = requires(T&& t) {
    typename T::element_type::ValueType;
    IsSender<typename T::element_type>;
};

template <typename T>
struct IsReceiverImpl : public std::false_type {};

template <typename T>
struct IsReceiverImpl<Receiver<T>> : public std::true_type {};

template <typename T>
inline constexpr auto IsReceiver = IsReceiverImpl<T>::value;

template <typename T>
concept IsReceiverPtr = requires(T&& t) {
    typename T::element_type::ValueType;
    IsReceiver<typename T::element_type>;
};

template <IsSenderPtr S>
struct SenderAdaptorClosure {
private:
    S& sender_;
public:
    constexpr explicit SenderAdaptorClosure(S& sender)
        : sender_(sender) {
    }

    template <std::ranges::viewable_range InputView>
    friend auto operator|(InputView&& lhs, SenderAdaptorClosure<S> self) {
        auto res = self.sender_->send(std::forward<InputView>(lhs));
        return std::ranges::single_view<bool>(res);
    }
};

struct SendView {
    template <IsSenderPtr S>
    auto operator()(S& sender) const {
        return SenderAdaptorClosure(sender);
    }
};

template <typename T>
using SenderPtr = std::unique_ptr<Sender<T>>;

template <typename T>
using SenderRefPtr = std::shared_ptr<Sender<T>>;

template <typename T>
using ReceiverPtr = std::unique_ptr<Receiver<T>>;

enum class ChannelEventType {
    Unknown,
    Success [[maybe_unused]],
    Closed,
    None,
};

template <typename T, typename = std::enable_if_t<!std::is_reference_v<T>> >
requires (std::movable<T>)
class Channel {
public:
    friend class Sender<T>;

    friend class Receiver<T>;

    static auto create() noexcept -> std::tuple<SenderPtr<T>, ReceiverPtr<T>> {
        auto p = std::shared_ptr<Channel<T>>(new Channel<T>(), [](Channel<T>* channel) {
            delete channel;
        });
        return { std::make_unique<Sender<T>>(p), std::make_unique<Receiver<T>>(p) };
    }

    inline auto done() noexcept -> void {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!isClosed_) {
            isClosed_ = true;
            cond_.notify_all();
        }
    }

    [[nodiscard]] inline auto isDone() noexcept -> bool {
        std::lock_guard<std::mutex> lock(mutex_);
        return isClosed_;
    }

    Channel(const Channel&) = delete;

    Channel(Channel&&) = delete;

    auto operator=(const Channel&) -> Channel& = delete;

    auto operator=(Channel&&) -> Channel& = delete;

private:
    Channel() = default;

    ~Channel() = default;

private:
    bool isClosed_ = false;
    std::list<T> messages_;
    std::mutex mutex_;
    std::condition_variable cond_;
};


template <typename T>
class Sender final {
public:
    using ValueType = T;
    explicit Sender(std::shared_ptr<Channel<T>> channel)
        : channel_(channel) {

    }

    ~Sender() {
        if (!channel_->isClosed_) {
            done();
        }
        channel_.reset();
    }

    template <typename ...Args>
    inline auto emplace(Args&&... args) -> bool
    requires std::constructible_from<T, Args...> {
        if (!channel_->isClosed_) {
            std::lock_guard<std::mutex> lock(channel_->mutex_);
            channel_->messages_.emplace_back(std::forward<Args>(args)...);
            return true;
        }
        return false;
    }

    template <typename I>
    inline auto send(const I& box) noexcept -> bool
    requires IsRange<I> && std::copyable<T> && std::is_convertible_v<typename decltype(box.begin())::value_type, T>  {
        if (box.empty()) {
            return true;
        }
        std::lock_guard<std::mutex> lock(channel_->mutex_);
        if (!channel_->isClosed_) {
            for (const auto& v : std::views::all(box)) {
                channel_->messages_.push_back(v);
            }
            channel_->cond_.notify_one();
            return true;
        }
        return false;
    }

    template <typename I>
    inline auto send(I&& box) noexcept -> bool
    requires IsRange<I> && std::is_convertible_v<typename decltype(box.begin())::value_type, T> {
        if (box.empty()) {
            return true;
        }
        std::lock_guard<std::mutex> lock(channel_->mutex_);
        if (!channel_->isClosed_ && !box.empty()) {
            if constexpr (std::is_rvalue_reference_v<decltype(box)>) {
                std::move(box.begin(), box.end(), std::back_inserter(channel_->messages_));
            } else {
                for (const auto& v : std::views::all(box)) {
                    channel_->messages_.push_back(v);
                }
            }
            channel_->cond_.notify_one();
            return true;
        }
        return false;
    }

    template <typename U>
    inline auto send(const U& message) noexcept -> bool
    requires std::copy_constructible<T> && std::is_convertible_v<U, T> {
        std::lock_guard<std::mutex> lock(channel_->mutex_);
        if (!channel_->isClosed_) {
            channel_->messages_.push_back(message);
            channel_->cond_.notify_one();
            return true;
        }
        return false;
    }

    template <typename U>
    inline auto send(U&& message) noexcept -> bool
    requires std::is_convertible_v<U, T> {
        std::lock_guard<std::mutex> lock(channel_->mutex_);
        if (!channel_->isClosed_) {
            channel_->messages_.emplace_back(std::forward<U>(message));
            channel_->cond_.notify_one();
            return true;
        }
        return false;
    }

    inline auto done() noexcept -> void {
        channel_->done();
    }

    [[nodiscard]] inline auto isDone() noexcept -> bool {
        return channel_->isDone();
    }

private:
    std::shared_ptr<Channel<T>> channel_;
};

namespace _detail {
    auto closedHandler = []{
        throw std::runtime_error("can't send message to closed channel.");
    };
}

template <typename U>
auto operator<<(const IsSenderPtr auto& sender, U&& message) -> const IsSenderPtr auto&
requires std::movable<U> && std::is_convertible_v<U, GetChannelType<decltype(sender)>> {
    if (!sender->send(std::forward<U>(message))) {
        _detail::closedHandler();
    }
    return sender;
}

template <typename U>
auto operator<<(const IsSenderPtr auto& sender, const U& message) -> const IsSenderPtr auto&
requires std::copyable<U> && std::is_convertible_v<U, GetChannelType<decltype(sender)>> {
    if (!sender->send(message)) {
        _detail::closedHandler();
    }
    return sender;
}

template <typename U>
auto operator<<(const IsSenderPtr auto& sender, const U& box) -> const IsSenderPtr auto&
requires IsRange<U> && std::is_convertible_v<typename decltype(box.begin())::value_type, GetChannelType<decltype(sender)>> {
    if (!sender->send(box)) {
        _detail::closedHandler();
    }
    return sender;
}

template <typename U>
auto operator<<(const IsSenderPtr auto& sender, U&& box) -> const IsSenderPtr auto&
requires IsRange<U> && std::is_convertible_v<typename decltype(box.begin())::value_type, GetChannelType<decltype(sender)>> {
    if (!sender->send(std::forward<U>(box))) {
        _detail::closedHandler();
    }
    return sender;
}

template <typename T>
class ReceiverIterator;

template <typename T>
class ConstReceiverIterator;

template <typename T>
class Receiver final {
private:
    friend class ReceiverIterator<T>;
    friend class ConstReceiverIterator<T>;
private:
    class ReceiverImpl {
    public:
        friend class Channel<T>;

        friend class ReceiverIterator<T>;

        explicit ReceiverImpl(std::shared_ptr<Channel<T>> channel)
            : channel_(channel) {

        }

        ~ReceiverImpl() {
            channel_->done();
        }

        inline auto operator=(const ReceiverImpl& src) noexcept -> ReceiverImpl& {
            channel_ = src.channel_;
        }

        ReceiverImpl(const ReceiverImpl& src) noexcept
            : channel_(src.channel_) {

        }

        inline auto operator=(ReceiverImpl&& src) noexcept -> ReceiverImpl& {
            channel_ = std::move(src.channel_);
        }

        ReceiverImpl(ReceiverImpl&& src) noexcept
            : channel_(std::move(src.channel_)) {

        }

        inline auto receive() noexcept -> std::expected<T, ChannelEventType>
        requires std::movable<T> {
            std::unique_lock<std::mutex> lock(channel_->mutex_);
            channel_->cond_.wait(lock, [this] {
                return !channel_->messages_.empty() || channel_->isClosed_;
            });
            if (channel_->isClosed_ && channel_->messages_.empty()) {
                return std::unexpected(ChannelEventType::Closed);
            }
            auto value = std::move(channel_->messages_.front());
            channel_->messages_.pop_front();
            return value;
        }

        inline auto tryReceive() noexcept -> std::expected<T, ChannelEventType> {
            std::unique_lock<std::mutex> lock(channel_->mutex_);
            if (channel_->messages_.empty()) {
                ChannelEventType type = ChannelEventType::Unknown;
                if (channel_->isClosed_) {
                    type = ChannelEventType::Closed;
                } else {
                    type = ChannelEventType::None;
                }
                return std::unexpected(type);
            }
            auto value = std::move(channel_->messages_.front());
            channel_->messages_.pop_front();
            return value;
        }

        inline auto tryReceiveAll() -> std::list<T> {
            decltype(channel_->messages_) messages;
            {
                std::lock_guard<std::mutex> lock(channel_->mutex_);
                messages.swap(channel_->messages_);
            }
            return messages;
        }
        
        inline auto size() -> uint32_t {
            uint32_t result = 0;
            {
                std::lock_guard<std::mutex> lock(channel_->mutex_);
                result = static_cast<uint32_t>(channel_->messages_.size());
            }
            return result;
        }

    private:
        std::shared_ptr<Channel<T>> channel_;
    };//end of class ReceiverImpl

    std::shared_ptr<ReceiverImpl> impl_;

public:
    using ValueType = T;
    explicit Receiver(std::shared_ptr<Channel<T>> channel)
        : impl_(std::make_shared<ReceiverImpl>(channel)) {

    }

    ~Receiver() noexcept = default;

    inline auto operator=(const Receiver<T>& src) -> Receiver<T>& {
        impl_ = src.impl_;
    }

    Receiver(const Receiver<T>& src) noexcept
        : impl_(src.impl_) {

    }

    inline auto operator=(Receiver<T>&& src) noexcept -> Receiver<T>& {
        impl_ = std::move(src.impl_);
    }

    Receiver(Receiver<T>&& src) noexcept
        : impl_(std::move(src.impl_)) {

    }

    inline auto begin() noexcept -> ReceiverIterator<T> {
        auto it = ReceiverIterator<T>(impl_, ChannelEventType::Unknown);
        it.value_ = std::move(impl_->receive());
        return it;
    }

    inline auto end() noexcept -> ReceiverIterator<T> {
        return ReceiverIterator<T>(impl_, ChannelEventType::Closed);
    }
    
    inline auto receive() noexcept -> std::expected<T, ChannelEventType> {
        return impl_->receive();
    }

    inline auto tryReceive() noexcept -> std::expected<T, ChannelEventType> {
        return impl_->tryReceive();
    }

    inline auto tryReceiveAll() noexcept -> std::list<T> {
        return impl_->tryReceiveAll();
    }
    
    inline auto size() noexcept -> uint32_t {
        return impl_->size();
    }

    inline auto empty() noexcept -> bool {
        return impl_->size() == 0;
    }
}; //end of class Receiver

template <typename U>
auto operator>>(const IsReceiverPtr auto& receiver, U& value) -> const IsReceiverPtr auto&
requires std::movable<U> && std::is_convertible_v<GetChannelType<decltype(receiver)> , U> {
    auto res = receiver->receive();
    if (res.has_value()) {
        value = std::move(*res);
    } else if (res.error() == ChannelEventType::Closed) {
        _detail::closedHandler();
    }
    return receiver;
}

template <typename U>
auto operator>>(const IsReceiverPtr auto& receiver, U& value) -> const IsReceiverPtr auto&
requires std::is_copy_assignable_v<U> && (!std::movable<U>) && std::is_convertible_v<GetChannelType<decltype(receiver)>, U> {
    auto res = receiver->receive();
    if (res.has_value()) {
        value = *res;
    } else if (res.error() == ChannelEventType::Closed) {
        _detail::closedHandler();
    }
    return receiver;
}


template <typename T>
class ReceiverIterator {
    friend class Receiver<T>;
protected:
    std::shared_ptr<typename Receiver<T>::ReceiverImpl> receiverImpl_;
    mutable std::expected<T, ChannelEventType> value_;

public:
    using iterator_category = std::input_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using reference = T&;

    explicit ReceiverIterator(std::shared_ptr<typename Receiver<T>::ReceiverImpl> receiverImpl,
                              ChannelEventType eventType = ChannelEventType::Unknown)
        : receiverImpl_(receiverImpl)
        , value_(std::unexpected(eventType)) {

    }

    ReceiverIterator()
        : receiverImpl_(nullptr)
        , value_(std::unexpected(ChannelEventType::Closed)) {

    }

    [[maybe_unused]] ReceiverIterator(const ReceiverIterator& src) noexcept
    requires std::copyable<T>
        : receiverImpl_(src.receiverImpl_)
        , value_(src.value_) {
    }

    inline auto operator=(const ReceiverIterator& src) noexcept -> ReceiverIterator& {
        receiverImpl_ = src.receiverImpl_;
        value_ = src.value_;
        return *this;
    }

    inline auto operator*() const noexcept -> reference {
        return *value_;
    }

    inline auto operator++() noexcept -> ReceiverIterator& {
        value_ = std::move(receiverImpl_->receive());
        return *this;
    }

    inline auto operator++(int) noexcept -> ReceiverIterator {
        auto temp = std::move(*this);
        *this = ReceiverIterator(receiverImpl_);
        this->value_ = receiverImpl_->receive();
        return temp;
    }

    inline auto operator==(const ReceiverIterator& iter) const noexcept -> bool {
        const auto& lvalue = value_;
        const auto& rvalue = iter.value_;
        if (lvalue.has_value() && rvalue.has_value()) {
            return &lvalue.value() == &rvalue.value();
        }
        if (lvalue.has_value() || rvalue.has_value()) {
            return false;
        }
        return lvalue.error() == rvalue.error();
    }

    inline auto operator!=(const ReceiverIterator& iter) const noexcept -> bool {
        return !operator==(iter);
    }
}; //end of class ReceiverIterator

} //end of namespace slark

namespace std::ranges::views { // NOLINT(cert-dcl58-cpp)

inline constexpr slark::SendView sender;

} //end of namespace std::ranges::views


