# Slark C++20 协程框架重构 Phase 4：move-only Task

> 状态：设计阶段
> 更新：2026-07-27
> 作者：Nevermore-

---

## 目录

1. [背景与动机](#1-背景与动机)
2. [Phase 4A：Task 核心](#2-phase-4atask-核心)
3. [Phase 4B：Awaiter 重写](#3-phase-4bawaiter-重写)
4. [Phase 4C：AsyncGenerator 与组合子](#4-phase-4casyncgenerator-与组合子)
5. [Phase 4D：用户代码迁移](#5-phase-4d用户代码迁移)
6. [测试策略](#6-测试策略)
7. [安全矩阵](#7-安全矩阵)
8. [回退策略](#8-回退策略)

---

## 1. 背景与动机

### 1.1 当前设计的问题

现阶段的 `Coroutine<T>` 使用 `CoroutineControlBlock`（`shared_ptr`）+ `SharedCoroutineHandle`（原子状态机）实现共享所有权，导致：

```
问题链：
SharedCoroutineHandle 共享所有权
  → 多人并发 resume 可能
    → 需要 resumeMutex_ 保护
      → final_suspend 通过 executor 恢复 continuation
        → symmetric transfer 链下 inlineExec 重入同一控制块
          → 普通 mutex 死锁
            → recursive_mutex 掩盖（当前修复）
```

这种设计源于早期"协程帧可能被多方独立持有"的假设，实际使用中**协程帧的生命周期始终由调用链上的某方独占**。共享所有权只解决了一个并不存在的并发问题，却引入了锁、原子 CAS、复杂状态机。

### 1.2 重构目标

1. **Task<T> 独占所有权** — 协程帧生命周期由 Task 唯一管理，`~Task()` 直接 `destroy()`，无 shared_ptr、无可变状态
2. **final_suspend 对称转移** — 恢复 parent 不走 executor，直接转移 coroutine_handle，**彻底消除重入**（与 folly/cppcoro 一致）
3. **取消绑定到具体 awaiter** — `cancelToken_` 不再全局传播，各 awaiter 按需接入
4. **EventLoop 使用 alive-flag 模式** — 防止异步回调在协程帧销毁后恢复悬挂 handle
5. **移除锁、移除原子状态机、移除 SharedCoroutineHandle**

### 1.3 设计原则

| 原则 | 含义 |
|------|------|
| **独占所有权** | Task 唯一持有 coroutine_handle，析构即销毁 |
| **对称转移** | 只有 await_suspend 的 symmetric transfer，没有 executor 绕行 |
| **上下文自动传播** | executor + eventLoop 通过 `inheritContext` 从父传到子 |
| **取消是合作式且本地化** | 取消只影响注册它的 awaiter，不自动传播 |
| **at-most-once resume** | 通过 alive-flag 保证，不再依赖 CAS 状态机 |
| **RAII 保活** | awaiter 在析构时自动注销所有回调 |
| **Executor 用于启动和切换** | `scheduleVia` 首次启动，`scheduleOn` 切换线程 |
| **EventLoop 可替换** | 通过 promise 传播 + `getDefault/setDefault`，无编译期单例依赖 |

---

## 2. Phase 4A：Task 核心

### 2.1 新增文件 `Task.hpp`

```cpp
// Task.hpp — 取代 Coroutine.hpp 的核心类型

namespace slark {

// ============= IExecutor — 抽象执行器 =============

class IExecutor {
public:
    virtual ~IExecutor() = default;

    virtual void execute(std::function<void()> task) noexcept = 0;
    virtual const char* name() const noexcept { return "IExecutor"; }

    // 是否为默认 inline executor（未显式设置 scheduleOn/scheduleVia）
    virtual bool isInline() const noexcept { return false; }
};

// 全局默认 executor：当前线程同步执行
inline IExecutor& inlineExecutor() noexcept {
    static InlineExecutor instance;
    return instance;
}

// convenience: lambda → IExecutor 适配器
class FunctionExecutor : public IExecutor {
public:
    template <typename F>
    explicit FunctionExecutor(F&& f) : fn_(std::forward<F>(f)) {}

    void execute(std::function<void()> task) noexcept override {
        fn_(std::move(task));
    }
    const char* name() const noexcept override { return "FunctionExecutor"; }

private:
    std::function<void(std::function<void()>)> fn_;
};

class InlineExecutor final : public IExecutor {
public:
    void execute(std::function<void()> task) noexcept override { task(); }
    const char* name() const noexcept override { return "InlineExecutor"; }
    bool isInline() const noexcept override { return true; }
};

// ============= TaskPromiseBase — 无锁、上下文传播 =============

struct TaskPromiseBase {
    std::coroutine_handle<> continuation_;
    IExecutor* executor_ = &inlineExecutor(); // 默认 inline，永不为空
    class EventLoop* eventLoop_ = nullptr;    // 可选，用于 fd 注册
    std::atomic<bool> completed_{false};

    struct FinalAwaiter {
        bool await_ready() const noexcept { return false; }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
            auto& promise = h.promise();
            promise.completed_.store(true, std::memory_order_release);
            if (promise.continuation_) {
                return promise.continuation_;   // 对称转移 → 无重入
            }
            return std::noop_coroutine();        // 顶层协程
        }

        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }
    std::suspend_always initial_suspend() noexcept { return {}; }

    void setContinuation(std::coroutine_handle<> cont) noexcept {
        continuation_ = cont;
    }
    std::coroutine_handle<> takeContinuation() noexcept { return std::exchange(continuation_, nullptr); }

    // 上下文继承：从父协程复制 executor 和 eventLoop 到当前 promise
    // executor_ 永不为空（默认 inlineExecutor），eventLoop_ 可选
    void inheritContext(const TaskPromiseBase& parent) noexcept {
        executor_ = parent.executor_;     // 直接覆盖默认值
        if (parent.eventLoop_) {
            eventLoop_ = parent.eventLoop_;
        }
    }

    // 已移除：cancelToken_、isCancelled()、makeResumeHandle()、
    //   trySetContinuation() 状态机、scheduleContinuation()
};

// ============= TaskPromise<T> =============

template <typename T>
class TaskPromise : public TaskPromiseBase {
public:
    Task<T> get_return_object() noexcept;

    void return_value(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        value_.emplace(std::move(value));
    }

    void unhandled_exception() noexcept { exception_ = std::current_exception(); }

    T result() {
        if (exception_) std::rethrow_exception(exception_);
        return std::move(*value_);
    }

private:
    std::optional<T> value_;
    std::exception_ptr exception_;
};

// ============= TaskPromise<void> =============

template <>
class TaskPromise<void> : public TaskPromiseBase {
public:
    Task<void> get_return_object() noexcept;

    void return_void() noexcept {}
    void unhandled_exception() noexcept { exception_ = std::current_exception(); }

    void result() {
        if (exception_) std::rethrow_exception(exception_);
    }

private:
    std::exception_ptr exception_;
};

// ============= Task<T> =============

template <typename T>
class [[nodiscard]] Task {
public:
    using promise_type = detail::TaskPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    Task() noexcept = default;

    explicit Task(handle_type handle) noexcept : handle_(handle) {}

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~Task() {
        if (handle_) handle_.destroy();
    }

    [[nodiscard]] bool isValid() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] bool isDone() const noexcept { return !handle_ || handle_.done(); }

    void resume() noexcept {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    T result() {
        if (!handle_) throw std::runtime_error("Task is not valid");
        return handle_.promise().result();
    }

    [[nodiscard]] handle_type getHandle() const noexcept { return handle_; }

    auto operator co_await() && noexcept {
        return TaskAwaiter<T>{std::exchange(handle_, nullptr)};
    }

    // 在指定 executor 上启动协程，设置 promise.executor_
    void scheduleVia(IExecutor& executor) noexcept {
        if (handle_ && !handle_.done()) {
            handle_.promise().executor_ = &executor;
            executor.execute([h = handle_]() noexcept { h.resume(); });
        }
    }

private:
    handle_type handle_ = nullptr;  // 独占所有权
};

// ============= TaskAwaiter<T> — 对称转移 + 上下文传播 =============

template <typename T>
class TaskAwaiter {
public:
    using handle_type = std::coroutine_handle<typename Task<T>::promise_type>;

    explicit TaskAwaiter(handle_type handle) noexcept : handle_(handle) {}

    bool await_ready() const noexcept { return !handle_ || handle_.done(); }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> awaiting) noexcept {
        // 在子协程的 promise 中设置续体为父协程
        handle_.promise().setContinuation(awaiting);
        // 子协程继承父协程的 executor 和 eventLoop（除非已显式设置）
        handle_.promise().inheritContext(awaiting.promise());
        // 对称转移：子协程在当前线程立即开始执行
        return handle_;
    }

    T await_resume() { return handle_.promise().result(); }

private:
    handle_type handle_ = nullptr;
};

} // namespace slark
```

### 2.2 关键设计决策

#### 2.2.1 为什么 `~Task()` 可以安全地 `handle_.destroy()`

因为 Task 是独占的。一个协程 handle 在任何时刻只能被一个 Task 持有。以下情况会破坏 Task：

```cpp
// 正确：move 转移所有权
auto child = someTask();
auto awaiter = std::move(child).operator co_await();  // child.handle_ = nullptr

// 正确：co_await 消费
co_await someTask();  // 临时 Task 在 await_resume 后析构

// 错误：如果 Task 被多方持有 → 不可能，move-only
```

#### 2.2.2 continuation_raw handle 的生命周期

`continuation_` 是父协程的 `coroutine_handle<>`。安全前提：

- 父协程在 `co_await child` 时将自己挂起
- 父协程的 Task 未被销毁（父 Task 在调用栈上，或在 AsyncScope 等容器中）
- 子协程完成时将父 coroutine_handle 通过 final_suspend 的 symmetric transfer 恢复
- 父恢复后，子协程帧被父协程栈上的 awaiter 析构或 Task 析构销毁

**不存在父先于子销毁的场景**，因为子协程帧的 `continuation_` 在 `final_suspend` 中被读取，而此时父一定还活着（父在等着子完成）。

#### 2.2.3 和当前设计的对比

| 特性 | 当前 `Coroutine<T>` | Phase 4 `Task<T>` |
|------|---------------------|-------------------|
| 所有权 | `shared_ptr<ControlBlock>` | 独占 coroutine_handle |
| frame 销毁 | `ControlBlock::~dtor` | `Task::~dtor` |
| final_suspend | `scheduleContinuation(executor, cont)` → 可能重入 | `return continuation_` → 对称转移 |
| executor 传播 | 从父复制到子 | 从父继承（通过 `inheritContext`） |
| eventLoop 传播 | 无 | 从父继承，`FdReadyAwaiter` 从 promise 读取 |
| 取消传播 | promise.cancelToken_ 自动复制 | **无**（由 awaiter 各自处理） |
| 锁 | `resumeMutex_` (recursive) | **无** |
| 原子状态 | SharedCoroutineHandle 3 态 CAS + 重入保护 | **无** |

### 2.3 Executor 调度与查询

#### 2.3.1 scheduleOn — 切换执行器

将当前协程切换到指定 executor 上恢复，并在 promise 中更新 `executor_`：

```cpp
struct ScheduleOnAwaiter {
    IExecutor& target_;

    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> h) noexcept {
        // 在目标 executor 上恢复，并更新 promise.executor_
        h.promise().executor_ = &target_;
        target_.execute([h]() noexcept { h.resume(); });
    }

    void await_resume() noexcept {}
};

// 工厂函数
inline auto scheduleOn(IExecutor& exec) noexcept {
    return ScheduleOnAwaiter{exec};
}
```

使用方式：

```cpp
Task<void> processData(IExecutor& ioExecutor, IExecutor& cpuExecutor) {
    auto data = co_await readFromIO();          // 在调用线程执行
    co_await scheduleOn(cpuExecutor);           // 切换到 CPU 线程
    auto result = heavyCompute(std::move(data));
    co_await scheduleOn(ioExecutor);            // 切回 IO 线程
    co_await writeResult(std::move(result));
}
```

**关键**：`scheduleOn` 后，`promise.executor_` 指向目标 executor，子协程通过 `TaskAwaiter::await_suspend` 的 `inheritContext` 自动继承。

#### 2.3.2 co_current_executor — 查询当前执行器

通过 `await_suspend` 返回 `false` 的"零开销查询"模式，读取 promise 中的 executor：

```cpp
struct CurrentExecutorAwaiter {
    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
        result_ = h.promise().executor_;  // 永不为空（默认 inlineExecutor）
        return false;                      // 不挂起，立即返回
    }

    IExecutor& await_resume() noexcept { return *result_; }

private:
    IExecutor* result_ = &inlineExecutor();
};

// 自由函数
inline auto co_current_executor() noexcept {
    return CurrentExecutorAwaiter{};
}
```

**零开销**：`await_suspend` 返回 `false` 导致协程不挂起，`await_resume` 立即返回 executor 值。编译器通常能内联整个调用链。

使用方式：

```cpp
Task<void> logWithExecutor() {
    auto& exec = co_await co_current_executor();
    // exec 永不为空（默认 inlineExecutor）
    std::cout << "running on executor: " << exec.name() << "\n";
}
```

#### 2.3.3 初始上下文设置

顶层协程默认在 `inlineExecutor()` 上执行。切换到指定 executor 有两种方式：

```cpp
// 方式1：Task.scheduleVia（设置 executor 并调度首次 resume）
auto task = someCoroutine();
task.scheduleVia(ioExecutor);

// 方式2：EventLoop 封装（同时设置 eventLoop_ + executor_）
eventLoop.spawn(task);  // 内部设置后 resume

// 方式3：先 co_await 再 resume（适用于需要先设置上下文）
auto task = someCoroutine();
task.promise().eventLoop_ = &eventLoop;
task.getHandle().resume();
```

`promise.executor_` 默认指向 `inlineExecutor()`，永不为空，无需显式初始化。

---

## 3. Phase 4B：Awaiter 重写

### 3.1 Awaiter 安全模型：alive-flag

所有需要注册异步回调的 awaiter 使用 **alive-flag** 模式防止悬挂指针：

```
awaiter 构造
  → aliveFlag_ = make_shared<atomic<bool>>(true)
  → 注册回调，传入 aliveFlag_ + coroutine_handle<>
  → co_await 挂起

回调触发时（可能在另一线程）：
  → if (aliveFlag_->load()) handle_.resume()   // 安全

awaiter 在正常完成的 await_resume 后析构：
  → *aliveFlag_ = false                         // 后续回调被抑制
  → 注销回调                                     // 二次保护

awaiter 被取消/协程被销毁时析构：
  → *aliveFlag_ = false
  → 注销回调
```

关键实现在 `EventLoop`、`TimerScheduler`、`DnsResolver` 等异步服务侧：

```cpp
// 回调侧的通用模式
void onEvent(...) {
    if (aliveFlag && aliveFlag->load(std::memory_order_acquire)) {
        handle.resume();  // 安全：协程帧一定存活
    }
}
```

### 3.2 TimerAwaiter

```cpp
class TimerAwaiter {
public:
    explicit TimerAwaiter(std::chrono::milliseconds duration) : duration_(duration) {}

    bool await_ready() const noexcept { return duration_.count() <= 0; }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle_ = handle;
        aliveFlag_ = std::make_shared<std::atomic<bool>>(true);

        auto weakFlag = std::weak_ptr<std::atomic<bool>>(aliveFlag_);
        timerId_ = TimerScheduler::instance().scheduleCallback(duration_, [weakFlag, handle_]() {
            auto flag = weakFlag.lock();
            if (flag && flag->load(std::memory_order_acquire)) {
                handle_.resume();
            }
        });
        return true;  // suspend
    }

    void await_resume() noexcept {}

    ~TimerAwaiter() {
        cancel();
    }

private:
    void cancel() noexcept {
        if (aliveFlag_) {
            *aliveFlag_ = false;
            aliveFlag_.reset();
        }
        if (timerId_.isValid()) {
            TimerScheduler::instance().cancel(timerId_);
        }
    }

    std::chrono::milliseconds duration_;
    std::coroutine_handle<> handle_;
    std::shared_ptr<std::atomic<bool>> aliveFlag_;
    TimerId timerId_;
};
```

**取消与 CancellationToken 集成**：

```cpp
// 可选：支持外部 CancellationToken
CancellationTokenRegistration registerCancel(
    CancellationToken token,
    std::coroutine_handle<> handle,
    std::shared_ptr<std::atomic<bool>> aliveFlag)
{
    return token.registerCallback([handle, aliveFlag]() {
        if (aliveFlag && aliveFlag->load(std::memory_order_acquire)) {
            handle.resume();
        }
    });
}
```

**关键安全分析**：

| 场景 | 保护 |
|------|------|
| 定时器触发时协程已正常完成 | `aliveFlag_` 已 false → 回调被抑制 |
| 协程被取消时定时器已触发 | 回调 `weakFlag.lock()` 失败 → 被抑制 |
| 定时器触发和协程析构并发 | CAS 或 weak_ptr + alive-flag 双重保护 |

### 3.3 CompletionAwaiter

```cpp
template <typename T>
class CompletionAwaiter {
public:
    ~CompletionAwaiter() {
        if (handle_) {
            *aliveFlag_ = false;
            handle_ = nullptr;
        }
    }

    bool await_ready() const noexcept { return state != Init; }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle_ = handle;
        aliveFlag_ = std::make_shared<std::atomic<bool>>(true);
        state_ = Waiting;
        return true;
    }

    T await_resume() {
        if (exception_) std::rethrow_exception(exception_);
        return std::move(*result_);
    }

    void complete(T value) noexcept {
        result_.emplace(std::move(value));
        finish();
    }

    void fail(std::exception_ptr e) noexcept {
        exception_ = std::move(e);
        finish();
    }

private:
    void finish() noexcept {
        auto expected = State::Waiting;
        if (state_.compare_exchange_strong(expected, State::Completed)) {
            if (aliveFlag_->load(std::memory_order_acquire)) {
                handle_.resume();
            }
        }
    }

    // 取消了 SharedCoroutineHandle，改用 raw handle + alive-flag
    std::coroutine_handle<> handle_;
    std::shared_ptr<std::atomic<bool>> aliveFlag_;
    std::optional<T> result_;
    std::exception_ptr exception_;
    enum State : uint8_t { Init, Waiting, Completed };
    std::atomic<State> state_{Init};
};
```

相比当前设计的变化：
- 移除 `SharedCoroutineHandle` 和绕 executor 恢复
- `finish()` 直接 `handle_.resume()`，不绕经 executor
- alive-flag 保护 handle 安全

### 3.4 AsyncIOOperation<T>

当前 `AsyncIOOperation` 有大量重复逻辑（`<T>` 和 `<void>` 特化几乎一样）。重写后通过 CRTP 消除重复。

```cpp
// ============= IOState 简化 =============

enum class IOState : uint8_t { Pending, Completed, Failed, TimedOut };

// ============= AsyncIOOperation 基类（CRTP） =============

template <typename Derived, typename T>
class AsyncIOOperationBase {
public:
    bool await_ready() const noexcept {
        return state_.load(std::memory_order_acquire) != IOState::Pending;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle_ = handle;
        aliveFlag_ = std::make_shared<std::atomic<bool>>(true);
        derived().scheduleTimeout();
        derived().startOperation();

        if (state_.load(std::memory_order_acquire) != IOState::Pending) {
            return false;  // 同步完成
        }

        // 注册取消回调（可选）
        if constexpr (requires { handle.promise().cancelToken_; }) {
            cancelReg_ = handle.promise().cancelToken_.registerCallback(
                [this]() { cancelOperation(); });
        }

        return true;  // suspend
    }

    IOResult<T> await_resume() {
        auto s = state_.load(std::memory_order_acquire);
        if (s == IOState::Failed || s == IOState::TimedOut) {
            return std::unexpected(error_.value_or(Error::NetworkError));
        }
        return derived().getResult();
    }

    void setTimeout(std::chrono::milliseconds t) noexcept { timeout_ = t; }

protected:
    AsyncIOOperationBase() = default;

    void complete(T value) noexcept {
        derived().storeResult(std::move(value));
        finish(IOState::Completed);
    }

    void fail(Error error) noexcept {
        error_ = error;
        finish(IOState::Failed);
    }

    int32_t fd_{-1};
    std::optional<Error> error_;

private:
    void finish(IOState newState) noexcept {
        auto expected = IOState::Pending;
        if (state_.compare_exchange_strong(expected, newState)) {
            cancelTimer();
            if (auto cleanup = std::exchange(cleanup_, nullptr)) cleanup();
            if (aliveFlag_ && aliveFlag_->load(std::memory_order_acquire)) {
                handle_.resume();  // 直接恢复，不绕 executor
            }
        }
    }

    void scheduleTimeout() {
        if (timeout_.count() <= 0) return;
        auto weakFlag = std::weak_ptr<std::atomic<bool>>(aliveFlag_);
        timerId_ = TimerScheduler::instance().scheduleCallback(timeout_,
            [this, weakFlag]() {
                auto flag = weakFlag.lock();
                if (flag && flag->load(std::memory_order_acquire)) {
                    error_ = Error::ConnectionTimeout;
                    finish(IOState::TimedOut);
                }
            });
    }

    void cancelTimer() {
        if (timerId_.isValid()) {
            TimerScheduler::instance().cancel(timerId_);
            timerId_ = {};
        }
    }

    Derived& derived() noexcept { return static_cast<Derived&>(*this); }

    // 核心：raw handle + alive-flag，不再有 ExecutorFunc 传参
    std::coroutine_handle<> handle_;
    std::shared_ptr<std::atomic<bool>> aliveFlag_;
    std::atomic<IOState> state_{IOState::Pending};
    std::function<void()> cleanup_;
    std::chrono::milliseconds timeout_{0};
    TimerId timerId_;
    CancellationTokenRegistration cancelReg_;
};

// ============= 具体派生类示例 =============

class AsyncConnect : public AsyncIOOperationBase<AsyncConnect, void> {
public:
    AsyncConnect(int32_t fd, std::string_view host, uint16_t port)
        : host_(host), port_(port) { fd_ = fd; }

    void startOperation() noexcept { /* ... */ }
    void cancelOperation() noexcept { /* ... */ }
    void storeResult(auto) noexcept {}
    void getResult() noexcept { /* SO_ERROR check */ }

private:
    std::string host_;
    uint16_t port_;
};
```

**安全分析**：

| 场景 | 保护 |
|------|------|
| `complete()` 在 `await_suspend` 返回前调用 | `state_ != Pending` → `await_suspend` 返回 false → 不挂起 |
| 定时器触发和 `complete()` 并发 | `state_` CAS 保证 only-one-wins |
| 协程帧销毁时 IO 仍在进行 | `aliveFlag_` 在 awaiter 析构中置 false → finish 中的 `resume()` 被抑制 |
| 取消和 IO 完成并发 | `state_` CAS + alive-flag 双重保护 |

### 3.5 EventLoop fd 注册安全

当前 EventLoop 使用 `SharedCoroutineHandle` + `RegistrationToken` + `takeContinuation` 模式。重写后需要使用 **alive-flag** 模式。

```cpp
// ============= EventLoop 新接口 =============

class EventLoop {
public:
    // 注册 struct：将回调所需信息打包
    struct FdRegistration {
        int32_t fd;
        FdInterest interest;
        std::coroutine_handle<> handle;
        std::shared_ptr<std::atomic<bool>> aliveFlag;
        // 用于取消
        uint64_t generation;  // 每次注册递增，防止 ABA
    };

    // 返回注册 ID（用于取消）
    using RegistrationId = uint64_t;

    RegistrationId registerContinuation(FdRegistration reg) noexcept;
    void unregisterContinuation(RegistrationId id) noexcept;

    // 内部：dispatchEvent 检查 alive-flag
    void dispatchEvent(int32_t fd, bool readable, bool writable, bool error) noexcept {
        auto regs = takeRegistrations(fd, readable, writable, error);
        for (auto& reg : regs) {
            if (reg.aliveFlag && reg.aliveFlag->load(std::memory_order_acquire)) {
                reg.handle.resume();
            }
        }
    }

    // 停止时：恢复所有 pending 协程（通知它们）
    void stop() noexcept {
        running_.store(false);
        wakeup();
        // 停止后残留的 registration：unregisterAll 时会设置 aliveFlag=false
        // 但协程不会再被恢复（aliveFlag 被设 false 阻止了 doResume 回调）
    }
};
```

**关键场景分析**：

| 场景 | 当前 `SharedCoroutineHandle` | Phase 4 alive-flag |
|------|----------------------------|-------------------|
| fd 就绪时协程已正常完成 | `notify()` 返回 false | `aliveFlag->load()` 返回 false |
| fd 就绪和取消/销毁并发 | `cancelWait()` 的 CAS | alive-flag 的 atomic 读 + 析构中的 unregister |
| stop() 时残留 pending handle | `unregisterAllContinuations` 移除，但不恢复 | unregister + aliveFlag=false + 不再 resume |
| 三方竞态（fd+timeout+cancel） | SharedCoroutineHandle 3 态 CAS | 各 awaiter 独立竞争，谁先 finish CAS state 谁赢 |

**重要的区别**：当前设计使用 CAS 仲裁胜利方，失败方放弃。新设计中**不存在三方竞争**——因为 timer 和 fd 注册在不同的 awaiter 上，每个 awaiter 独立完成。取消也是通过 `CancellationToken` 的 callback 分别通知每个 awaiter。

### 3.6 FdReadyAwaiter — 从 promise 读取 EventLoop，去单例

```cpp
class FdReadyAwaiter {
public:
    enum class Interest : uint8_t { Read, Write };

    // 显式指定 loop 时覆盖 promise 传播的，为 nullptr 时走 resolveEventLoop
    FdReadyAwaiter(int32_t fd, Interest interest, EventLoop* loop = nullptr) noexcept
        : fd_(fd), interest_(interest), explicitLoop_(loop) {}

    ~FdReadyAwaiter() {
        if (aliveFlag_) {
            *aliveFlag_ = false;
            aliveFlag_.reset();
        }
        if (regId_.has_value() && eventLoop_) {
            eventLoop_->unregisterContinuation(*regId_);
        }
    }

    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle_ = handle;
        aliveFlag_ = std::make_shared<std::atomic<bool>>(true);

        // 确定 EventLoop：优先级 explicitLoop_ > promise.eventLoop_ > EventLoop::getDefault()
        eventLoop_ = (explicitLoop_ ? explicitLoop_
                      : (handle.promise().eventLoop_ ? handle.promise().eventLoop_
                         : EventLoop::getDefault()));

        auto fdInterest = (interest_ == Interest::Read) ? FdInterest::Read : FdInterest::Write;
        regId_ = eventLoop_->registerContinuation({
            fd_, fdInterest, handle_, aliveFlag_, nextGeneration()
        });

        if (interest_ == Interest::Read) {
            eventLoop_->setReadable(fd_, true);
        } else {
            eventLoop_->setWritable(fd_, true);
        }

        // 注册取消（aliveFlag 保护：取消可能在 awaiter 析构后触发）
        if constexpr (requires { handle.promise().cancelToken_; }) {
            cancelReg_ = handle.promise().cancelToken_.registerCallback(
                [this, aliveFlag = aliveFlag_]() {
                    if (aliveFlag && aliveFlag->load(std::memory_order_acquire)) {
                        cancel();
                    }
                });
        }

        return true;
    }

    void await_resume() noexcept {}

private:
    void cancel() noexcept {
        if (aliveFlag_) {
            *aliveFlag_ = false;
            aliveFlag_.reset();
        }
        if (regId_.has_value() && eventLoop_) {
            eventLoop_->unregisterContinuation(*regId_);
            regId_.reset();
        }
        if (handle_) {
            handle_.resume();
        }
    }

    int32_t fd_;
    Interest interest_;
    EventLoop* explicitLoop_ = nullptr;    // 构造时显式指定
    EventLoop* eventLoop_ = nullptr;       // await_suspend 中 resolve 的结果
    std::coroutine_handle<> handle_;
    std::shared_ptr<std::atomic<bool>> aliveFlag_;
    std::optional<EventLoop::RegistrationId> regId_;
    CancellationTokenRegistration cancelReg_;
};
```

**EventLoop 默认值替代单例**：

```cpp
class EventLoop {
public:
    // 全局默认 EventLoop（线程安全，调用方负责生命周期）
    static EventLoop* getDefault() noexcept {
        return defaultLoop_.load(std::memory_order_acquire);
    }
    static void setDefault(EventLoop* loop) noexcept {
        defaultLoop_.store(loop, std::memory_order_release);
    }

    // ... registerContinuation / unregisterContinuation 等接口不变 ...

private:
    inline static std::atomic<EventLoop*> defaultLoop_{nullptr};
};
```

**设计意图**：

- **传播优先**：协程通过 `co_await child()` 自动继承 `eventLoop_`，无需手动传入
- **显式覆盖**：构造时传入 `EventLoop*` 覆盖传播，用于测试 mock 或同时使用多个 EventLoop
- **全局兜底**：`EventLoop::getDefault()` 作为单例替代，保证向后兼容
- **无 IOContext 单例依赖**：`FdReadyAwaiter` 不再硬编码 `IOContext::instance()`

### 3.7 CancellationToken 模型变更

**不再自动传播**：`PromiseBase` 不再持有 `cancelToken_`，`TaskAwaiter` 不会从父传播到子。

```cpp
// 使用模式变更

// 旧：取消自动传播
Task<void> parentTask(CancellationToken token) {
    // token 自动传播到 child 的 promise.cancelToken_
    co_await childTask(token);
}

// 新：取消是 per-awaiter 的
Task<void> parentTask(CancellationToken token) {
    co_await childTask(token);  // 内部 awaiter 按需提取 cancelToken
    // TimerAwaiter / FdReadyAwaiter 通过 `if constexpr` 检测 cancelToken
    // 如果父 promise 有 cancelToken_ 字段，则注册取消回调
}
```

**`CancellationToken` 类本身不变**（`shared_ptr<State>` + callback map），但取消路径改为：

```
旧：token.cancel()
  → 遍历 callbackMap
  → 每个 callback 通过 SharedCoroutineHandle 恢复
  → SharedCoroutineHandle 状态机仲裁

新：token.cancel()
  → 遍历 callbackMap  
  → 每个 callback 检查 alive-flag
  → 直接 handle_.resume()
```

**取消注册的 RAII** 保持不变：`CancellationTokenRegistration` 析构时自动 `unregister`。

#### 3.7.1 co_withCancellation — 语法糖

将任意 awaiter 包装为可取消版本，比手动 `withCancellation` 更简洁：

```cpp
template <typename Awaiter>
class CancellableWrapper {
public:
    CancellableWrapper(CancellationToken token, Awaiter&& awaiter)
        : token_(std::move(token)), awaiter_(std::forward<Awaiter>(awaiter)) {}

    auto await_ready() { return awaiter_.await_ready(); }

    template <typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> h) {
        aliveFlag_ = std::make_shared<std::atomic<bool>>(true);
        // alive-flag 保护：token 取消时 awaiter 可能已析构
        cancelReg_ = token_.registerCallback([h, flag = aliveFlag_]() {
            if (flag && flag->load(std::memory_order_acquire)) {
                h.resume();
            }
        });
        return awaiter_.await_suspend(h);  // 转发对称转移 / suspend 决策
    }

    auto await_resume() {
        cancelReg_ = {};                    // RAII 自动注销
        if (aliveFlag_) { *aliveFlag_ = false; aliveFlag_.reset(); }
        return awaiter_.await_resume();
    }

    ~CancellableWrapper() {
        if (aliveFlag_) { *aliveFlag_ = false; aliveFlag_.reset(); }
    }

private:
    CancellationToken token_;
    Awaiter awaiter_;
    CancellationTokenRegistration cancelReg_;
    std::shared_ptr<std::atomic<bool>> aliveFlag_;
};

// 工厂函数
template <typename Awaiter>
auto co_withCancellation(CancellationToken token, Awaiter&& awaiter) {
    return CancellableWrapper<Awaiter>(std::move(token), std::forward<Awaiter>(awaiter));
}
```

使用示例：

```cpp
// 基本用法：任何 awaiter 都可以包装
co_await co_withCancellation(token, TimerAwaiter(5s));
co_await co_withCancellation(token, FdReadyAwaiter(fd, Read));
co_await co_withCancellation(token, asyncRead(socket, buf));

// 无 token 时自然省略包装——和普通 co_await 无区别
co_await TimerAwaiter(5s);   // 不可取消
co_await FdReadyAwaiter(fd, Read);  // 不可取消
```

### 3.8 EventLoop 实现细节

#### 3.8.1 并发安全的 fd 注销

EventLoop 本身可能在 `dispatchEvent` 回调中执行 `unregisterContinuation`（来自 awaiter 的 `await_resume` 和析构）。

```cpp
void EventLoop::dispatchEvent(int32_t fd, bool readable, bool writable, bool error) noexcept {
    // 1. 摘除（需要互斥）
    std::vector<FdRegistration> regs;
    {
        std::lock_guard<std::mutex> lock(fdMutex_);
        regs = takeRegistrations(fd, readable, writable, error);
    }

    // 2. 恢复（锁外执行，防止死锁）
    for (auto& reg : regs) {
        if (reg.aliveFlag && reg.aliveFlag->load(std::memory_order_acquire)) {
            reg.handle.resume();    // 此时 awaiter 析构可能会调 unregisterContinuation
        }
    }
}

void EventLoop::unregisterContinuation(RegistrationId id) noexcept {
    std::lock_guard<std::mutex> lock(fdMutex_);
    registrations_.erase(id);      // 这里 registry 可能已被 dispatchEvent 的操作删除
}
```

**重要**：`resume()` 在锁外执行，所以 awaiter 析构中的 `unregisterContinuation` 不会死锁。

#### 3.8.2 边缘情况：dispatchEvent 和 unregisterContinuation 对同一 fd 并发

| 线程 A | 线程 B |
|--------|--------|
| `dispatchEvent` `lock(fdMutex_)` | |
| 读取 regs | |
| `unlock(fdMutex_)` | |
| | `lock(fdMutex_)` |
| | `unregisterContinuation(fd)` — 已不在表中，安全 |
| | `unlock(fdMutex_)` |
| `for (reg : regs) { check aliveFlag; resume; }` | |

这里 B 的 unregister 不产生实际效果（A 已经取走了 regs），而 A 的 resume 是安全的（aliveFlag 在 awaiter 中还未被设 false，因为 awaiter 在 resume 后才析构）。

#### 3.8.3 stop() 时残留的协程

```cpp
void EventLoop::stop() noexcept {
    running_.store(false, std::memory_order_release);
    
    // 停止所有 pending 的 fd 等待
    std::vector<FdRegistration> pendingRegs;
    {
        std::lock_guard<std::mutex> lock(fdMutex_);
        for (auto& [id, reg] : registrations_) {
            if (reg.aliveFlag) *reg.aliveFlag = false;
        }
        registrations_.clear();
    }
    // 不 resume 任何协程（让协程的 awaiter 发现 EventLoop 停止后自行处理）
    wakeup();
}
```

---

## 4. Phase 4C：AsyncGenerator 与组合子

### 4.1 AsyncGenerator 适配策略

`AsyncGenerator` 是当前问题最大的文件（722 行），因为它大量使用 `SharedCoroutineHandle` 和 `CoroutineControlBlock`。但本质逻辑不需要大改：

**核心概念保持不变**：
- 生产者协程 `co_yield` 值
- `AsyncGeneratorNextAwaiter` 从消费者 `co_await gen.next()` 中恢复生产者
- 消费者恢复后读取 `promise.value_`

**需要改变的地方**：
- `AsyncGeneratorPromise` 不再继承 `PromiseBase`，而是独立的 promise 类
- `AsyncGeneratorYieldAwaiter` 中移除 `SharedCoroutineHandle` 相关代码
- 消费者和生产者之间用 raw `coroutine_handle<>` 传递

```cpp
// ============= AsyncGeneratorPromise（简化版） =============

template <typename T>
class AsyncGeneratorPromise {
public:
    using value_type = T;
    using reference_type = T&;

    AsyncGenerator<T> get_return_object() noexcept;

    std::suspend_always initial_suspend() noexcept { return {}; }

    // final_suspend：生产者结束时恢复消费者
    struct FinalAwaiter {
        bool await_ready() const noexcept { return false; }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
            auto& promise = h.promise();
            promise.setCompleted();
            if (auto consumer = promise.takeWaitingConsumer()) {
                return consumer;   // 对称转移回消费者
            }
            return std::noop_coroutine();
        }

        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }

    void return_void() noexcept { setCompleted(); }
    void unhandled_exception() noexcept {
        exception_ = std::current_exception();
        setCompleted();
    }

    // yield_value：yield 后恢复消费者
    struct YieldAwaiter {
        bool await_ready() const noexcept { return false; }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise>) noexcept {
            if (auto consumer = promise_.takeWaitingConsumer()) {
                return consumer;   // 对称转移回消费者
            }
            return std::noop_coroutine();
        }

        void await_resume() noexcept {}
    private:
        AsyncGeneratorPromise& promise_;
        friend AsyncGeneratorPromise;
        explicit YieldAwaiter(AsyncGeneratorPromise& p) noexcept : promise_(p) {}
    };

    YieldAwaiter yield_value(T value) noexcept(...) {
        value_.emplace(std::move(value));
        return YieldAwaiter{*this};
    }

    // … 其余方法保持 …

    void setWaitingConsumer(std::coroutine_handle<> consumer) noexcept {
        consumerHandle_ = consumer;
    }

    std::coroutine_handle<> takeWaitingConsumer() noexcept {
        return std::exchange(consumerHandle_, nullptr);
    }

private:
    std::optional<T> value_;
    std::exception_ptr exception_;
    std::coroutine_handle<> consumerHandle_ = nullptr;  // raw handle
    std::atomic<bool> completed{false};
};
```

**AsyncGenerator<T> 本身**：

```cpp
template <typename T>
class [[nodiscard]] AsyncGenerator {
public:
    using promise_type = detail::AsyncGeneratorPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    explicit AsyncGenerator(handle_type handle) noexcept : handle_(handle) {}

    AsyncGenerator(AsyncGenerator&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}

    AsyncGenerator& operator=(AsyncGenerator&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~AsyncGenerator() {
        if (handle_) handle_.destroy();
    }

    auto next() & noexcept {
        return AsyncGeneratorNextAwaiter<T>{handle_};
    }

    auto next() && noexcept {
        return AsyncGeneratorNextAwaiter<T>{std::exchange(handle_, nullptr)};
    }

    // … transform/filter/take/drop 等方法保持不变 …

private:
    handle_type handle_ = nullptr;  // 独占所有权
};
```

**AsyncGeneratorNextAwaiter**：

```cpp
template <typename T>
class AsyncGeneratorNextAwaiter {
public:
    using promise_type = detail::AsyncGeneratorPromise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    explicit AsyncGeneratorNextAwaiter(handle_type handle) noexcept
        : producerHandle_(handle) {}

    bool await_ready() noexcept {
        if (!producerHandle_) return true;
        auto& promise = producerHandle_.promise();
        return promise.hasValue() || promise.isCompleted();
    }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> consumer) noexcept {
        auto& promise = producerHandle_.promise();
        if (promise.hasValue() || promise.isCompleted()) {
            return consumer;  // 已有值/已完成 → 不挂起消费者
        }
        promise.setWaitingConsumer(consumer);  // 消费者挂起在 producer promise 中
        return producerHandle_;                 // 对称转移到生产者
    }

    std::optional<T> await_resume() {
        // … 同当前实现 …
    }

private:
    handle_type producerHandle_ = nullptr;
};
```

**为什么不需要 alive-flag**：`AsyncGenerator` 的消费者和生产者之间通过对称转移同步执行。没有外部回调注册，所以不存在异步回调访问悬挂 handle 的问题。

### 4.2 AsyncScope

```cpp
class AsyncScope {
public:
    AsyncScope() : state_(std::make_shared<State>()) {}

    ~AsyncScope() {
        // 析构时取消但不等待
        state_->cancelSource.cancel();
        // childTasks_ 析构 → 所有子协程帧被 destroy（硬取消）
    }

    // spawn：启动 fire-and-forget 协程
    template <typename T>
    void spawn(Task<T> task) {
        state_->activeCount.fetch_add(1, std::memory_order_relaxed);

        // 包装协程：执行子任务 → 捕获异常 → 减计数 → 通知
        // 注意：必须作为 Task<void> 包装，确保子协程帧生命周期由 AsyncScope 管理
        auto wrapper = [state = state_, t = std::move(task)]() -> Task<void> {
            // 等待子任务完成（不传播异常到 AsyncScope）
            try {
                co_await std::move(t);
            } catch (...) {
                // 子任务异常在此吞没，不中断 AsyncScope
            }
            state->activeCount.fetch_sub(1, std::memory_order_release);
            state->cv.notify_one();
        };

        // 创建 wrapper Task，move 所有权到 childTasks_ 保活，再启动
        auto wrapperTask = wrapper();
        auto handle = wrapperTask.getHandle();
        state_->childTasks.push_back(std::move(wrapperTask));  // 保活

        if (state_->defaultExecutor) {
            state_->defaultExecutor->execute([h = handle]() noexcept { h.resume(); });
        } else {
            handle.resume();
        }
    }

    void waitAll() noexcept {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->cv.wait_for(lock, std::chrono::seconds(5),
            [s = state_]() { return s->activeCount.load(std::memory_order_acquire) == 0; });
        // 超时后强制取消
        if (state_->activeCount.load(std::memory_order_acquire) != 0) {
            state_->cancelSource.cancel();
            // childTasks_ 清空 → 所有子协程帧 destroy
        }
    }

    void cancelAll() noexcept { state_->cancelSource.cancel(); }

    void setDefaultExecutor(IExecutor& exec) noexcept {
        state_->defaultExecutor = &exec;
    }

private:
    struct State {
        std::atomic<int> activeCount{0};
        CancellationTokenSource cancelSource;
        std::vector<Task<void>> childTasks;  // 保活子协程帧（Task 独占所有权）
        std::mutex mutex;
        std::condition_variable cv;
        IExecutor* defaultExecutor = nullptr;
    };

    std::shared_ptr<State> state_;
};
```

**AsyncScope 的 spawn 变化**：不再通过 `schedule(handle)`（经 `Scheduler` 的兜底或 ThreadPool），而是直接 `handle.resume()` 或通过自定义 executor。子协程帧的生命周期由 `state_->childTasks` 中的 `Task<void>` 保活。wrapper 协程内的 `try/catch` 确保子任务异常不传播到 AsyncScope 析构。

**线程安全**：
- `spawn` 可以跨线程调用（`activeCount` 是 atomic，`childTasks` 通过 `State` 的 `shared_ptr` 跨线程共享——但 `std::vector<Task<void>>` 的并发 push_back 需要外部同步。建议：要么 spawn 只从单线程调用，要么加 mutex 保护）
- `cancelAll` 可以跨线程调用（`CancellationTokenSource` 是线程安全的）
- `~AsyncScope` 只能从单线程调用（析构本身不是线程安全的）

### 4.3 WhenAll / WhenAny / WithTimeout

**核心模式**：每个子任务通过 named helper coroutine 包装，子任务完成时通知共享 state。父协程挂起等待最后一个子任务完成。与当前设计的不同：不再绕 executor 恢复，改为子任务直接 `continuation.resume()`。

**生命周期关键**：
- `whenAllChild` 返回 `Task<void>`，其协程帧由 `WhenAllAwaiter` 中的 `childTasks_` vector 保活
- 子任务完成时 `continuation.resume()` 恢复父协程，父协程继续 → `WhenAllAwaiter` 析构 → `childTasks_` 销毁 → 已完成子协程的 `~Task()` 调用 `handle_.destroy()`，此时 `handle_.done() == true`，销毁安全
- 未完成的子任务在 `~Task()` 中被硬取消（`handle_.destroy()`），这是预期行为
- `continuation` 是 raw `coroutine_handle<>`，父协程在 `await_suspend` 中挂起，一定存活

```cpp
// ============= WhenAllState =============

template <typename... Ts>
struct WhenAllState {
    std::tuple<std::optional<Ts>...> results;
    std::atomic<int> remaining;
    std::exception_ptr exception;
    std::coroutine_handle<> continuation;  // 父协程 handle
};

// ============= 命名 helper：子任务包装 =============

template <size_t I, typename... Ts>
Task<void> whenAllChild(std::shared_ptr<WhenAllState<Ts...>> state,
                        Task<std::tuple_element_t<I, std::tuple<Ts...>>> task) {
    try {
        auto value = co_await std::move(task);
        std::get<I>(state->results).emplace(std::move(value));
    } catch (...) {
        if (!state->exception) {
            state->exception = std::current_exception();
        }
    }

    auto prev = state->remaining.fetch_sub(1, std::memory_order_acq_rel);
    if (prev == 1 && state->continuation) {
        state->continuation.resume();   // 最后一个完成，直接恢复父协程
    }
    // 协程进入 final_suspend → continuation_ 为空 → std::noop_coroutine()
    // ~Task() 安全销毁（handle_.done() == true）
}
// 注意：whenAllChild 的 continuation_（TaskPromiseBase 的字段）为空，
// 因为 WhenAllAwaiter 没有设置它。这是设计意图——恢复走 state->continuation 直接 resume。

// ============= WhenAllAwaiter =============

template <typename... Ts>
class WhenAllAwaiter {
public:
    explicit WhenAllAwaiter(std::tuple<Task<Ts>...> tasks)
        : tasks_(std::move(tasks))
        , state_(std::make_shared<WhenAllState<Ts...>>()) {
        state_->remaining = sizeof...(Ts);
    }

    // 可选：传入 executor 用于并行调度子任务（覆盖 inheritContext 传播的 executor）
    void scheduleVia(IExecutor& exec) noexcept { defaultExecutor_ = &exec; }

    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        state_->continuation = handle;

        // executor：优先级 defaultExecutor_ > promise.executor_（永不为空）
        IExecutor* exec = defaultExecutor_ ? defaultExecutor_ : handle.promise().executor_;

        startTasks(exec, std::index_sequence_for<Ts...>{});

        return std::noop_coroutine();  // 父挂起
    }

    std::tuple<Ts...> await_resume() {
        if (state_->exception) std::rethrow_exception(state_->exception);
        return collectResults(std::index_sequence_for<Ts...>{});
    }

private:
    template <size_t I>
    void startOne(IExecutor* exec) {
        // 创建子 wrapper → move 所有权到 childTasks_ → 启动
        auto child = whenAllChild<I>(state_, std::move(std::get<I>(tasks_)));
        auto handle = child.getHandle();

        childTasks_.push_back(std::move(child));  // 保活：所有权转移

        if (exec) {
            exec->execute([h = handle]() noexcept { h.resume(); });  // 并行启动
        } else {
            handle.resume();                                          // 同步启动（串行）
        }
    }

    template <size_t... Is>
    void startTasks(IExecutor* exec, std::index_sequence<Is...>) {
        (startOne<Is>(exec), ...);
    }

    template <size_t... Is>
    std::tuple<Ts...> collectResults(std::index_sequence<Is...>) {
        return std::make_tuple(std::move(*std::get<Is>(state_->results))...);
    }

    std::tuple<Task<Ts>...> tasks_;
    std::shared_ptr<WhenAllState<Ts...>> state_;
    std::vector<Task<void>> childTasks_;     // 保活子协程帧（Task 独占所有权）
    IExecutor* defaultExecutor_ = nullptr;
};
```

**`when_all` 工厂函数**：

```cpp
// 基本版本：子任务通过 inheritContext 从父协程继承 executor
template <typename... Ts>
auto when_all(Task<Ts>... tasks) {
    return WhenAllAwaiter<Ts...>(std::make_tuple(std::move(tasks)...));
}

// 带 executor 版本：覆盖 propagation，所有子任务在指定 executor 上并行启动
template <typename... Ts>
auto when_all(IExecutor& exec, Task<Ts>... tasks) {
    auto awaiter = WhenAllAwaiter<Ts...>(std::make_tuple(std::move(tasks)...));
    awaiter.scheduleVia(exec);
    return awaiter;
}
```

**Executor 传播**：`whenAllChild` 内部 `co_await std::move(task)` 触发 `TaskAwaiter::inheritContext`，子子任务自动继承 executor + eventLoop。这意味着只要顶层设置了 executor，整个 when_all 子树都能感知到。

**生命周期时间线**：

```
1. await_suspend 中：
   startOne 创建 child（Task<void>，栈上局部变量）
   → child.getHandle() 复制 handle
   → childTasks_.push_back(std::move(child))  // 所有权转移，child.handle_ = nullptr
   → handle.resume()  // 启动子协程，或者 exec([h=handle](){...}) 并行启动

2. 子协程 co_await task（等待真正的子任务完成）：
   父已挂起。子任务完成后 → whenAllChild 的 co_await 返回

3. remaining.fetch_sub(1) → prev=1（最后一个）：
   → state->continuation.resume() 恢复父协程
   → whenAllChild 进入 co_return → final_suspend
     → continuation_ 为空 → std::noop_coroutine()
     → handle_.done() == true

4. 父协程恢复 → await_resume → WhenAllAwaiter 析构：
   → childTasks_ 析构 → 遍历所有 Task<void>
   → 已完成的子 Task：handle_.done() == true → destroy() 安全
   → 未完成的子 Task：handle_.done() == false → destroy() 硬取消（预期）
```

**WhenAny** 同理：`whenAnyChild` 中第一位完成的子任务设置 `state->continuation.resume()`，其他未完成的在 `WhenAnyAwaiter` 析构时被 `childTasks_` 销毁。

**WithTimeout** 不再需要独立框架文件，直接用 `when_any` 组合：

```cpp
auto result = co_await when_any(
    ioExec,
    task(),
    []() -> Task<void> { co_await TimerAwaiter(timeout); }()
);
```

---

## 5. Phase 4D：用户代码迁移

### 5.1 全局替换清单

| 旧类型/函数 | 新类型/函数 | 迁移模式 |
|------------|------------|---------|
| `Coroutine<T>` | `Task<T>` | 全局替换，move-only 语义一致 |
| `Coroutine<void>` | `Task<void>` | 同上 |
| `co_await coro` | `co_await std::move(task)` | 可能已如此 |
| `coro.makeResumeHandle()` | `task.getHandle()` | 改为直接获取 raw handle |
| `scheduler.schedule(handle)` | `executor->execute([h](){ h.resume(); })` | 替换所有调度点 |
| `co_await TimerAwaiter(1s)` | 不变 | 内部实现变了，接口不变 |
| `co_await asyncRead(...)` | `co_await withCancellation(token, asyncRead(...))` | 按需包装取消 |
| `shared_from_this()` in CallbackBridge | 移除或保持 | CallbackBridge 重写 |

### 5.2 HttpStream / HLSStream 迁移

**当前**：

```cpp
class HttpStream {
    Coroutine<void> coroutine_;   // 保活协程
    // ...
    void start() {
        coroutine_ = streamTask(...);
        coroutine_.resume();
    }
};
```

**Phase 4**：

```cpp
class HttpStream {
    Task<void> task_;             // 保活 Task（move-only，所有权明确）
    std::coroutine_handle<> handle_;
    // ...
    void start() {
        task_ = streamTask(...);
        handle_ = task_.getHandle();
        // 通过 executor 启动，自动设置 promise.executor_
        ioExecutor_->execute([h = handle_]() { h.resume(); });
    }
};
```

**注意**：`Task<T>` 析构会 `destroy()` 协程帧，所以 `HttpStream` 的析构不再需要手动管理协程生命周期。但需要确保 `task_` 在合适的时机被赋值（如 move assignment），否则旧协程帧会被销毁。

### 5.3 CallbackBridge 迁移

**当前**——使用 `Coroutine<HttpResponse>` + `weak_from_this()`：

```cpp
template <typename LaunchFunc>
class CallbackBridge : public std::enable_shared_from_this<...> {
    Coroutine<HttpResponse> requestCoroutine_;
    Coroutine<void> monitorCoroutine_;
    // ...
};
```

**Phase 4**——改用 `Task` + `weak_ptr`：

```cpp
template <typename LaunchFunc>
class CallbackBridge : public std::enable_shared_from_this<...> {
    Task<HttpResponse> requestTask_;
    std::coroutine_handle<> requestHandle_;

    void run() {
        requestTask_ = launcher_(requestInfo_, token_);
        requestHandle_ = requestTask_.getHandle();

        // 启动协程（在线程池上）
        pool_.postJob([h = requestHandle_]() { h.resume(); });
    }

    ~CallbackBridge() {
        cancel();
        waitForExit();
    }
};
```

### 5.4 删除的文件

重构完成后可以删除：

1. `src/base/coroutine/CoroutineControlBlock.hpp`
2. `src/base/coroutine/SharedCoroutineHandle.hpp`
3. `src/base/coroutine/Coroutine.hpp`（被 `Task.hpp` 取代）
4. `src/base/coroutine/WithTimeout.hpp`（用 `when_any` + `TimerAwaiter` 取代）

---

## 6. 测试策略

### 6.1 测试重写优先级

| 优先级 | 测试文件 | 策略 |
|--------|---------|------|
| P0 | `CoroutineTest.cpp` (35 tests) | 全部重写为 Task，验证核心语义 |
| P0 | `CancellationPropagationTest.cpp` (8 tests) | 验证 per-awaiter 取消，移除自动传播测试 |
| P0 | `AsyncIOOperationTest.cpp` (9 tests) | 验证新 alive-flag 模式 |
| P1 | `AsyncGeneratorTest.cpp` (49 tests) | 重写 AsyncGenerator 调用的框架代码 |
| P1 | `WhenAllTest.cpp` / `WhenAnyTest.cpp` | 重写组合子测试 |
| P1 | `AsyncScopeTest.cpp` | 重写 scope 测试 |
| P1 | `YieldTest.cpp` / `ExecutorTest.cpp` | 验证 executor 语义变化 |
| P2 | `CoroutineContractTest.cpp` / `CoroutineControlBlockTest.cpp` / `CoroutineAbiTest.cpp` | 验证新契约 |
| P2 | `CoroutineStateMachineFuzzTest.cpp` | 新模糊测试（alive-flag 并发） |

### 6.2 新增测试

| # | 测试 | 覆盖场景 |
|---|------|----------|
| 1 | `TaskMoveOnlySemantics` | move 后源 invalid：析构不 destroy 已转移的 frame |
| 2 | `FinalAwaiterSymmetricTransfer` | 子完成直接对称转移恢复父，不经过 executor |
| 3 | `AliveFlagConcurrentFire` | 多线程并发触发 alive-flag，只有一个成功 resume |
| 4 | `AliveFlagAfterAwaiterDestructed` | awaiter 销毁后回调不 resume 悬挂 handle |
| 5 | `CancellationTokenPerAwaiter` | 取消只影响注册该 token 的 awaiter |
| 6 | `WhenAllManyTasks` | 10 个并行子任务 |
| 7 | `WhenAllAwaiterDestructedBeforeChildren` | 父被取消时子协程帧被安全销毁 |
| 8 | `WithCancellationAndTimer` | `withCancellation` 包装取消定时器 |

### 6.3 回归检查

改造后必须运行的检查：

```bash
# 全量基础测试
./test/base/Base_test

# ASAN 泄漏检测（move-only 所有析构必须正确）
ASAN_OPTIONS=detect_leaks=1 ./test/base/Base_test

# 模糊测试（alive-flag 并发）
./test/fuzz/BaseFuzz_test

# HTTP 集成测试
./test/http/HTTP_test

# 核心集成测试
./test/core/CoreIntegration_test
```

---

## 7. 安全矩阵

### 7.1 各场景安全分析

| # | 场景 | 当前设计 | Phase 4 设计 | 安全 |
|---|------|---------|-------------|------|
| 1 | fd 就绪时协程已正常完成 | `SharedCoroutineHandle::notify()` → false | `aliveFlag_←false` → 回调抑制 | ✅ |
| 2 | fd 就绪和协程销毁并发 | `cancelWait()` CAS | alive-flag atomic load + `weakFlag.lock()` | ✅ |
| 3 | 定时器触发和 complete 并发 | IOState CAS | IOState CAS + alive-flag | ✅ |
| 4 | 三方竞争（fd+timer+cancel） | SharedCoroutineHandle 3 态 CAS | 各 awaiter 独立 IOState CAS，彼此不感知 | ✅ |
| 5 | EventLoop stop 残留 handle | handle 不恢复（`!running_` 直接 return） | stop 中设 aliveFlag=false，handle 不恢复 | ✅ |
| 6 | 子协程完成恢复父（symmetric transfer） | `scheduleContinuation` → 可能重入 | 直接 `return continuation_` → 对称转移 | ✅ |
| 7 | AsyncScope 析构时子协程仍运行 | childControls → shared_ptr 保活 | childTasks（`vector<Task<void>>`）析构 → `handle_.destroy()` 硬取消 | ✅ |
| 8 | WhenAll 父析构时子仍在运行 | childControls → shared_ptr | childTasks_ 析构 → `handle_.destroy()` 硬取消未完成子任务 | ✅ |
| 9 | CancellationToken 回调在 awaiter 析构后触发 | 依赖 Registration 析构 unregister | alive-flag 双重保护（Registration unregister + alive-flag load） | ✅ |
| 10 | complete() 在 await_suspend() 之前调用 | — | finish() 处理 Init→Completed CAS，await_ready 返回 true 跳过挂起 | ✅ |

### 7.2 对比：当前 vs Phase 4

| 维度 | 当前 | Phase 4 |
|------|------|---------|
| 核心原语 | `shared_ptr<ControlBlock>` + 原子状态机 | `coroutine_handle<>` + alive-flag |
| 协程帧生命周期 | ControlBlock 析构时 destroy | Task 析构时 destroy（独占） |
| executor 传播 | 从父复制到子 | `inheritContext` 从父继承；`scheduleOn` 切换；`co_current_executor` 查询 |
| eventLoop 传播 | 无 | `inheritContext` 从父继承；`FdReadyAwaiter` 从 promise 读取；`EventLoop::getDefault()` 兜底 |
| 并发 resume 保护 | `resumeMutex_` | 无（不存在并发 resume） |
| 取消传播 | promise.cancelToken_ 自动传播 | per-awaiter 按需 `withCancellation` |
| CancellationToken 回调安全 | `trySetContinuation` CAS 仲裁 | alive-flag atomic load 保护 |
| final_suspend 恢复 | `scheduleContinuation(executor, cont)` | 对称转移 `return continuation_` |
| WhenAll 子任务保活 | shared_ptr 引用控制块 | `vector<Task<void>>` 独占所有权 |
| AsyncScope 子任务保活 | childControls shared_ptr | `vector<Task<void>>` 独占 + activeCount 递减 |
| 核心锁+原子 | 2 种 mutex + 4 种原子 CAS | 0 种 mutex + 2 种原子 CAS（IOState + aliveFlag） |
| 竞态源头 | 多人持同一 handle | 各 awaiter 独立回调，不互知 |
| 代码量 | ~1900 行框架 + 用户代码 | ~1200 行框架（-37% 预估） |

---

## 8. 回退策略

### 8.1 分阶段回退

每个 Phase 独立提交、独立验证。如果 Phase 4B 出现问题，Phase 4A 的 Task 核心可以保留，回退只在 Phase 4B 范围内。

### 8.2 并行保留旧代码

在迁移期间，`Coroutine<T>` 和 `Task<T>` 可以**共存于同一编译单元**：

```cpp
// 用户代码平滑过渡
using Task = Coroutine;  // 或 using Coroutine = Task;
```

但这不是终极方案——最终目标是将 `Coroutine<T>` 完全移除。

### 8.3 关键风险点

| 风险 | 等级 | 缓解 |
|------|------|------|
| alive-flag 在弱内存序平台上的可见性 | 中 | 全部使用 `memory_order_acquire/release` |
| `handle_.resume()` 在 `final_suspend` 的 symmetric transfer 链中多次 | 低 | `FinalAwaiter` 只转移一次 |
| 用户代码中隐式 `co_await` 取消传播被消除但未察觉 | 中 | **Breaking change**，需在文档和编译期警告中说明 |
| EventLoop 锁外 resume 导致 awaiter 析构抢先 unregister | 低 | dispatchEvent 在锁内已摘除 reg，锁外 resume 时的 unregister 找不到 reg → no-op |
| WhenAll/AsyncScope 中 Task<void> 生命周期管理（双销毁） | 中 | 已修正：`vector<Task<void>>` 独占所有权，move 后源 `handle_` 置空；子任务完成时 `handle_.done() == true`，`destroy()` 安全 |
| WhenAll 子任务串行启动 | 中 | 已修正：支持 `IExecutor&` 参数，可并行调度 |<｜end▁of▁thinking｜>

