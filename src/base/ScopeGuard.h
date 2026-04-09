#include <functional>
#include <utility>

namespace slark {

class ScopeGuard {
public:
    template <typename F>
    explicit ScopeGuard(F&& fn) : fn_(std::forward<F>(fn)) {}

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    ScopeGuard(ScopeGuard&& other) noexcept = delete;

    ScopeGuard& operator=(ScopeGuard&& other) = delete;

    ~ScopeGuard() {
        release();
    }

    void dismiss() noexcept {
        active_ = false;
    }

    void release() {
        if (active_ && fn_) {
            fn_();
        }
        active_ = false;
        fn_ = nullptr; // 防止double free
    }

private:
    std::function<void()> fn_;
    bool active_{true};
};

} // namespace slark