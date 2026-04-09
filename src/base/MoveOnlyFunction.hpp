//
// Created by Nevermore on 2025/4/1.
// slark MoveOnlyFunction — polyfill for std::move_only_function (C++23)
// Copyright (c) 2025 Nevermore All rights reserved.
//
#pragma once

#include <version>

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
#include <functional>

namespace slark {
template <typename Sig>
using MoveOnlyFunction = std::move_only_function<Sig>;
} // namespace slark
#else

#include <memory>
#include <type_traits>
#include <utility>

namespace slark {

template <typename Sig>
class MoveOnlyFunction;

template <typename R, typename... Args>
class MoveOnlyFunction<R(Args...)> {
public:
    MoveOnlyFunction() noexcept = default;

    MoveOnlyFunction(std::nullptr_t) noexcept {}

    template <typename F>
        requires(!std::is_same_v<std::decay_t<F>, MoveOnlyFunction> &&
                 std::is_invocable_r_v<R, F, Args...>)
    MoveOnlyFunction(F&& f) : impl_(std::make_unique<Model<std::decay_t<F>>>(std::forward<F>(f))) {}

    MoveOnlyFunction(MoveOnlyFunction&&) noexcept = default;
    MoveOnlyFunction& operator=(MoveOnlyFunction&&) noexcept = default;

    MoveOnlyFunction(const MoveOnlyFunction&) = delete;
    MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;

    R operator()(Args... args) {
        return impl_->invoke(std::forward<Args>(args)...);
    }

    R operator()(Args... args) const {
        return impl_->invoke(std::forward<Args>(args)...);
    }

    explicit operator bool() const noexcept {
        return impl_ != nullptr;
    }

    MoveOnlyFunction& operator=(std::nullptr_t) noexcept {
        impl_.reset();
        return *this;
    }

private:
    struct Concept {
        virtual ~Concept() = default;
        virtual R invoke(Args... args) = 0;
    };

    template <typename F>
    struct Model final : Concept {
        F func;

        explicit Model(F&& f) : func(std::move(f)) {}

        explicit Model(const F& f) : func(f) {}

        R invoke(Args... args) override {
            return std::invoke(func, std::forward<Args>(args)...);
        }
    };

    std::unique_ptr<Concept> impl_;
};

} // namespace slark

#endif
