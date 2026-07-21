# Slark C++20 协程框架设计

> 状态：已实现
> 更新：2026-07-21
> 作者：Nevermore-
> 关联代码：`src/base/coroutine/`、`src/http/IOContext.h`、`src/core/HttpStream*`、`src/core/HLSStream*`

---

## 目录

1. [框架概述](#1-框架概述)
2. [模块划分与模块职责](#2-模块划分与模块职责)
3. [模块详解](#3-模块详解)
   - [3.1 CoroutineControlBlock —— 生命周期仲裁者](#31-coroutinecontrolblock--生命周期仲裁者)
   - [3.2 Coroutine<T> —— 用户侧所有权句柄](#32-coroutinet--用户侧所有权句柄)
   - [3.3 SharedCoroutineHandle —— 跨异步边界恢复令牌](#33-sharedcoroutinehandle--跨异步边界恢复令牌)
   - [3.4 PromiseBase —— Promise 基础设施](#34-promisebase--promise-基础设施)
   - [3.5 CoroutineAwaiter / FinalAwaiter —— 协程嵌套机制](#35-coroutineawaiter--finalawaiter--协程嵌套机制)
   - [3.6 AsyncGenerator<T> —— 异步生成器](#36-asyncgeneratort--异步生成器)
   - [3.7 AsyncIOOperation<T> —— 异步 IO 操作基类](#37-asyncioperationt--异步-io-操作基类)
   - [3.8 TimerAwaiter / CompletionAwaiter —— 工具 Awaiter](#38-timerawaiter--completionawaiter--工具-awaiter)
   - [3.9 CoroutineScheduler —— 协程调度器](#39-coroutinescheduler--协程调度器)
   - [3.10 EventLoop —— IO 事件循环](#310-eventloop--io-事件循环)
   - [3.11 CancellationToken —— 取消传播](#311-cancellationtoken--取消传播)
4. [模块间协作](#4-模块间协作)
5. [核心设计决策](#5-核心设计决策)
6. [实际使用模式](#6-实际使用模式)
7. [测试策略](#7-测试策略)
8. [改造历史](#8-改造历史)

---

## 1. 框架概述

### 1.1 要解决什么问题

slark 是一个 C++20 协程驱动的网络/流媒体客户端库。协程被多方异步持有：用户代码、EventLoop、定时器、取消令牌、IO 操作、线程池。核心挑战：

- **生命周期管理**：协程帧何时销毁？谁有权限销毁？
- **竞态控制**：fd 就绪、超时、取消三方同时到达时，谁负责恢复协程？恢复几次？
- **异常传播**：嵌套协程间异常如何传递？
- **可组合性**：多个异步操作如何链式组合？数据如何流式传递？
- **线程安全**：不同线程上的 awaiter 如何正确协作？

### 1.2 设计原则

| 原则 | 含义 |
|------|------|
| **统一所有权** | 协程帧只有一个生命周期仲裁者，所有持有者通过 shared_ptr 引用它 |
| **at-most-once resume** | 无论有多少个完成源，协程只被恢复一次 |
| **提前通知不丢失** | 通知在挂起之前到达时，协程不挂起，直接继续执行 |
| **统一入口** | 所有 awaiter 通过同一接口创建恢复令牌 |
| **最小化共享状态** | 尽量减少跨线程共享的可变状态，用 CAS 替代 mutex |
| **RAII 至上** | 不暴露手动 release/destroy 接口，生命周期由析构函数自动管理 |

### 1.3 与 C++20 标准协程的关系

C++20 只提供了最基础的协程语言设施：

- `co_await` / `co_return` / `co_yield` 关键字
- `coroutine_handle`（裸指针语义，移动-only）
- Promise 协议（`get_return_object`、`initial_suspend`、`final_suspend`、`return_value`、`unhandled_exception`）

标准不提供：
- 协程帧的共享所有权（`coroutine_handle` 无引用计数）
- 协程的调度（谁负责 resume 协程、在哪个线程执行）
- 协程间的组合（如何 co_await 另一个协程并获取结果）
- 取消机制
- 超时机制
- 异步生成器（produce-consumer 模式）

**slark 的协程框架就是填补这些空白的完整实现。**

---

## 2. 模块划分与模块职责

整个协程框架分为 **11 个模块**，按职责分为三个层次：

### 层次结构

```
┌─────────────────────────────────────────────────────────────┐
│                    应用层（使用模式）                          │
│  HttpStream / HLSStream / HttpRequest / AsyncSocket 等       │
│  直接使用 Coroutine<T> / AsyncGenerator<T> / AsyncIOOperation │
└───────────────────────┬─────────────────────────────────────┘
                        │
┌───────────────────────┴─────────────────────────────────────┐
│                    中间件层（调度与事件）                      │
│  ┌──────────────┐  ┌───────────┐  ┌─────────────────┐      │
│  │ Coroutine    │  │ EventLoop │  │ CancellationToken │      │
│  │ Scheduler    │  │ (IO多路复用)│  │ (取消传播)       │      │
│  └──────────────┘  └───────────┘  └─────────────────┘      │
└───────────────────────┬─────────────────────────────────────┘
                        │
┌───────────────────────┴─────────────────────────────────────┐
│                    基础设施层（协程原语）                      │
│  ┌────────────┐ ┌─────────────────┐ ┌──────────────┐      │
│  │ Coroutine  │ │ SharedCoroutine │ │ Coroutine    │      │
│  │ ControlBlock│ │ Handle          │ │ Awaiter      │      │
│  └────────────┘ └─────────────────┘ └──────────────┘      │
│  ┌────────────┐ ┌─────────────────┐ ┌──────────────┐      │
│  │ PromiseBase│ │ AsyncGenerator  │ │ AsyncIO      │      │
│  │            │ │                 │ │ Operation    │      │
│  └────────────┘ └─────────────────┘ └──────────────┘      │
│  ┌────────────┐ ┌─────────────────┐                       │
│  │ Timer      │ │ Completion      │                       │
│  │ Awaiter    │ │ Awaiter         │                       │
│  └────────────┘ └─────────────────┘                       │
└───────────────────────────────────────────────────────────┘
```

### 模块清单

| # | 模块 | 文件 | 层次 | 核心职责 |
|---|------|------|------|----------|
| 1 | **CoroutineControlBlock** | `CoroutineControlBlock.hpp` | 基础设施 | 协程帧生命周期的唯一仲裁者 |
| 2 | **Coroutine\<T\>** | `Coroutine.hpp` | 基础设施 | 用户侧的 RAII 所有权句柄 + Promise 协议实现 |
| 3 | **SharedCoroutineHandle** | `SharedCoroutineHandle.hpp` | 基础设施 | 跨异步边界的恢复令牌（多生产者单消费者） |
| 4 | **PromiseBase** | `Coroutine.hpp`（内联） | 基础设施 | 协程 Promise 公共基类，统一恢复令牌创建入口 |
| 5 | **CoroutineAwaiter / FinalAwaiter** | `Coroutine.hpp`（内联） | 基础设施 | 协程嵌套组合机制 |
| 6 | **AsyncGenerator\<T\>** | `AsyncGenerator.hpp` | 基础设施 | 异步生产者-消费者生成器 |
| 7 | **AsyncIOOperation\<T\>** | `AsyncIOOperation.hpp` | 基础设施 | 异步 IO 操作基类（超时 + 取消 + 状态机） |
| 8 | **TimerAwaiter / CompletionAwaiter** | `Awaitable.hpp` | 基础设施 | 通用工具 Awaiter |
| 9 | **CoroutineScheduler** | `CoroutineScheduler.hpp` | 中间件 | 协程调度器（线程池 + 定时器） |
| 10 | **EventLoop** | `EventLoop.h/.cpp` | 中间件 | IO 多路复用（epoll/kqueue） |
| 11 | **CancellationToken** | `CancellationToken.h` | 中间件 | 取消信号传播 |

---

## 3. 模块详解

### 3.1 CoroutineControlBlock —— 生命周期仲裁者

**文件**：`src/base/coroutine/CoroutineControlBlock.hpp`

**职责**：协程 frame 生命周期的唯一所有者。析构时调用 `handle.destroy()`。

```cpp
class CoroutineControlBlock {
public:
    explicit CoroutineControlBlock(std::coroutine_handle<> handle) noexcept : handle_(handle) {}

    ~CoroutineControlBlock() {
        if (handle_) {
            handle_.destroy();  // 最后一个 shared_ptr 析构时触发
        }
    }

    bool isDone() const noexcept {
        return completed_.load(std::memory_order_acquire) || (handle_ && handle_.done());
    }

    void resume() noexcept {
        if (handle_ && !isDone()) {
            handle_.resume();
        }
    }

    void cancel() noexcept { cancelled_.store(true, std::memory_order_release); }
    bool isCancelled() const noexcept { return cancelled_.load(std::memory_order_acquire); }

private:
    std::coroutine_handle<> handle_ = nullptr;
    std::atomic<bool> completed_{false};
    std::atomic<bool> cancelled_{false};
    std::mutex resumeMutex_;
};
```

#### 为什么独立为一个单独的模块？

**最核心的设计决策**：为什么不把生命周期管理放在 `Coroutine<T>` 或 `Promise` 内部？

| 方案 | 问题 |
|------|------|
| 放在 `Coroutine<T>` 内 | `Coroutine<T>` 析构时如果 frame 还被 `SharedCoroutineHandle` 持有，会 UAF |
| 放在 Promise 内 | Promise 是协程帧的子对象，frame 销毁时 Promise 也销毁——无法在 frame 存活时独立管理 |
| **独立为 CoroutineControlBlock** | 独立于协程帧（不是 frame 的子对象），通过 shared_ptr 引用计数自动管理 |

**结论**：`CoroutineControlBlock` 不是协程帧的一部分，而是**协程帧的管理者**。所有持有者通过 `shared_ptr<CoroutineControlBlock>` 引用它，最后一个引用释放时自动 `handle.destroy()`。

#### 为什么需要 `mutex` 而非纯原子？

- `tryResume()` 路径通过 `SharedCoroutineHandle::resumed_` 原子 CAS 保证 at-most-once，不需要 mutex
- 但 `Coroutine::resume()` 直接调 `control_->resume()`，可能被多个线程同时调用
- `handle_.resume()` 本身不是线程安全的，C++ 标准规定同一协程的 `resume()` 不能并发执行
- **取舍**：多一个 mutex 锁开销，换取 `Coroutine::resume()` 的线程安全性。实际场景中 `resume()` 直接调用极少，不影响性能。

### 3.2 Coroutine\<T\> —— 用户侧所有权句柄

**文件**：`src/base/coroutine/Coroutine.hpp`

**职责**：
- 作为 `get_return_object()` 的返回类型，是用户代码持有协程的 RAII 句柄
- 构造时创建 `CoroutineControlBlock`
- 析构时释放 `shared_ptr`（不一定导致 frame 销毁）
- 提供 `share()` 将所有权转移给 `SharedCoroutineHandle`
- 实现 `co_await` 运算符，支持嵌套协程

#### 核心设计

```cpp
template <typename T>
class [[nodiscard]] Coroutine : public NonCopyable {
public:
    explicit Coroutine(handle_type handle) noexcept : handle_(handle) {
        if (handle_) {
            control_ = std::make_shared<CoroutineControlBlock>(handle_);
            handle_.promise().setControl(control_);  // promise 保存 weak_ptr
        }
    }

    ~Coroutine() = default;  // 仅 control_.reset()，不直接调 handle.destroy()

    SharedCoroutineHandle share() && noexcept {
        auto control = std::move(control_);
        handle_ = nullptr;
        return SharedCoroutineHandle(std::move(control));
    }

    auto operator co_await() const& noexcept {  // 左值 co_await
        return detail::CoroutineAwaiter<T>{control_, handle_};
    }

    std::shared_ptr<SharedCoroutineHandle> makeResumeHandle() const noexcept {
        if (!control_) return nullptr;
        return std::make_shared<SharedCoroutineHandle>(control_);
    }
};
```

#### 设计决策

**Q: 为什么 `Coroutine<T>` 不直接持有 `promise` 指针？**
- 因为生命周期不由 `Coroutine<T>` 独占管理，`CoroutineControlBlock` 才是仲裁者
- `Coroutine<T>` 只持有 `shared_ptr<CoroutineControlBlock>`，通过它间接访问 frame

**Q: 为什么 `~Coroutine()` 是 `= default`？**
- 不调 `handle.destroy()`——如果还有其他 `SharedCoroutineHandle` 持有 `CoroutineControlBlock`，frame 应该继续存活
- 这是整个框架最关键的设计决策：**销毁权归最后一个引用者**

**Q: 为什么重新支持左值 `co_await`？**
- 旧方案禁止左值 `co_await`（因为 `Coroutine<T>` 析构后 frame 可能被销毁，左值引用悬挂）
- 新方案通过 `shared_ptr<CoroutineControlBlock>` 保证：即使 `Coroutine<T>` 被析构，`CoroutineAwaiter` 中持有的 `control_` 让 frame 继续存活
- 这极大提升了 API 可用性——用户可以直接 `co_await coro` 而不必 `co_await std::move(coro)`

### 3.3 SharedCoroutineHandle —— 跨异步边界恢复令牌

**文件**：`src/base/coroutine/SharedCoroutineHandle.hpp`

**职责**：多生产者单消费者的恢复令牌。多个异步完成源（fd 就绪、超时、取消）共享同一令牌，但协程只被恢复一次。

#### 原子状态机

```
                   suspend() 成功
    Registering ──────────────────> Suspended
         │                              │
         │ notify() 先到达              │ notify() 后到达
         ▼                              ▼
      Signaled ───── suspend() ───> Scheduled ──> tryResume()
                      看到 Signaled，
                      跳过挂起
```

```cpp
// 由 await_suspend 在所有等待源注册完成后调用
[[nodiscard]] bool suspend() noexcept {
    auto expected = State::Registering;
    if (state_.compare_exchange_strong(expected, State::Suspended, ...)) {
        return true;   // 正常挂起
    }
    if (expected == State::Signaled) {
        state_.store(State::Scheduled, ...);  // 通知已到达，跳过挂起
    }
    return false;  // 不挂起
}

// 由事件/定时器/取消完成时调用
[[nodiscard]] bool notify() noexcept {
    auto expected = State::Registering;
    if (state_.compare_exchange_strong(expected, State::Signaled, ...)) {
        return false;  // 通知在挂起前到达，suspend() 会处理
    }
    expected = State::Suspended;
    return state_.compare_exchange_strong(expected, State::Scheduled, ...);
}

// at-most-once resume
bool tryResume() noexcept {
    bool expected = false;
    if (!resumed_.compare_exchange_strong(expected, true, ...) || done()) {
        return false;
    }
    control_->resume();
    return true;
}
```

#### 为什么需要两个原子变量（`state_` + `resumed_`）？

**`state_`** 解决的是 **"通知 vs 挂起"的时序竞态**：
- 场景：`await_suspend` 中先注册 fd 到 EventLoop，再调用 `suspend()`
- EventLoop 可能在 `suspend()` 之前触发（fd 就绪）
- 如果不用状态机，`notify()` 时协程还未挂起，`notify()` 不知道该做什么

**`resumed_`** 解决的是 **"多个完成源"的竞态**：
- 场景：fd 就绪 + 超时同时触发，两个线程同时调 `tryResume()`
- `resumed_` 的 CAS 保证只有一个线程能成功执行 `control_->resume()`

**为什么不能合并为一个**？因为解决的是两个正交的问题：
- `state_` 解决的是 await_suspend 内部竞态
- `resumed_` 解决的是 await_suspend 之后的 resume 竞态

### 3.4 PromiseBase —— Promise 基础设施

**文件**：`src/base/coroutine/Coroutine.hpp`（内联定义）

**职责**：所有协程 Promise 类型的公共基类，提供统一的基础设施。

```cpp
struct PromiseBase {
    std::shared_ptr<SharedCoroutineHandle> continuation_;  // 父协程的 continuation
    std::weak_ptr<CoroutineControlBlock> control_;         // 本协程的控制块（weak）
    std::atomic<bool> completed{false};

    // ★ 统一入口：所有 awaiter 通过它创建 SharedCoroutineHandle
    std::shared_ptr<SharedCoroutineHandle> makeResumeHandle() const noexcept {
        auto control = control_.lock();
        if (!control) return nullptr;  // frame 已销毁
        return std::make_shared<SharedCoroutineHandle>(std::move(control));
    }

    void markCompleted() noexcept {
        completed.store(true, std::memory_order_release);
        if (auto control = control_.lock()) {
            control->markCompleted();
        }
    }
};
```

#### 设计决策

**Q: 为什么 `control_` 用 `weak_ptr` 而非 `shared_ptr`？**
- Promise 是协程帧的子对象，frame 存活时 promise 也存活
- 如果 `control_` 用 `shared_ptr`，则 `CoroutineControlBlock` 永远无法被析构（promise 和 control block 互相持有）
- `weak_ptr` 打破循环引用：promise 通过 `lock()` 临时提升为 `shared_ptr`，使用完后释放

**Q: 为什么 `makeResumeHandle()` 是统一入口？**
- 所有 awaiter（TimerAwaiter、AsyncIOOperation、FdReadyAwaiter、CancellableFdReadyAwaiter 等）都需要创建 `SharedCoroutineHandle`
- 统一入口保证所有 `SharedCoroutineHandle` 共享同一份 `CoroutineControlBlock`，引用计数正确
- 如果 `control_.lock()` 返回 `nullptr`（frame 已销毁），统一返回 `nullptr`，调用方不挂起协程
- 消除重复代码，降低引入新 bug 的概率

### 3.5 CoroutineAwaiter / FinalAwaiter —— 协程嵌套机制

**文件**：`src/base/coroutine/Coroutine.hpp`（内联定义）

**职责**：实现 `co_await Coroutine<T>` 的语义——当前协程挂起，子协程恢复执行，子协程完成后恢复当前协程。

#### CoroutineAwaiter（co_await 的实现）

```cpp
template <typename T>
class CoroutineAwaiter {
public:
    bool await_ready() const noexcept {
        return !control_ || control_->isDone();  // 子协程已完成则不挂起
    }

    template <typename ParentPromise>
    std::coroutine_handle<> await_suspend(
            std::coroutine_handle<ParentPromise> awaiting) noexcept {
        if (auto parent = awaiting.promise().makeResumeHandle()) {
            (void)parent->suspend();       // 1. 父协程挂起
            handle_.promise().setContinuation(std::move(parent));  // 2. 注册 continuation
        }
        return handle_;  // 3. 恢复子协程执行
    }

    T await_resume() { return handle_.promise().getResult(); }
};
```

**流程**：
1. `await_suspend` 通过 `makeResumeHandle()` 创建父协程的恢复令牌
2. `parent->suspend()` 挂起父协程（如果子协程已经完成，这里返回 false，父协程不挂起）
3. 将父协程的令牌存入子协程的 promise（`setContinuation`）
4. 返回子协程的 `handle`，编译器恢复子协程执行

#### FinalAwaiter（子协程完成时的回调）

```cpp
struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    void await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto& promise = handle.promise();
        promise.markCompleted();
        if (auto continuation = promise.takeContinuation()) {
            if (continuation->notify()) {
                CoroutineScheduler::instance().schedule(std::move(continuation));
            }
        }
    }
};
```

**设计要点**：
- `final_suspend` 返回 `FinalAwaiter`，`await_ready` 返回 `false`，`await_suspend` 执行完不返回 handle——编译器默认不挂起协程（协程已完成）
- `schedule()` 在后台线程池执行 `tryResume()`，避免在 final_suspend 中直接递归 resume 父协程导致栈溢出

### 3.6 AsyncGenerator\<T\> —— 异步生成器

**文件**：`src/base/coroutine/AsyncGenerator.hpp`

**职责**：实现生产者-消费者模式的异步数据流。生产者通过 `co_yield` 生产数据，消费者通过 `co_await gen.next()` 消费数据。

#### 为什么需要单独的 AsyncGenerator？

C++20 标准不提供 `co_yield` 的消费者端 API。`co_yield` 只是挂起生产者，但没有机制让消费者"取走"值。`AsyncGenerator` 填补这个空白：

- 生产者用 `co_yield` 发布数据
- 消费者用 `co_await gen.next()` 消费数据
- 支持管道操作符（`| filter | transform | take`），惰性求值

#### 生产者-消费者生命周期

```
消费者                          生产者
  │                               │
  │  co_await gen.next()           │
  ├── await_suspend ──────────────►│  消费者挂起，恢复生产者
  │                               ├── co_yield value
  │◄── consumer.resume() ────────┤  生产者挂起，恢复消费者
  ├── await_resume()               │
  │  取到值                        │
  │                               │
  │  co_await gen.next()           │
  ├── await_ready=false ─────────►│  消费者挂起，恢复生产者
  │                               ├── ... 继续生产
```

#### 消费者存活检查

**关键设计**：生产者在 `takeWaitingConsumer()` 中检查消费者 frame 是否存活：

```cpp
std::coroutine_handle<> takeWaitingConsumer() noexcept {
    if (consumerState_ == ConsumerState::Waiting) {
        consumerState_ = ConsumerState::Resumed;
        auto h = std::exchange(consumerHandle_, nullptr);
        // 检查 consumer frame 是否仍存活
        if (auto control = consumerControl_.lock()) {
            (void)control;
        } else {
            return nullptr;  // 消费者已销毁，不恢复
        }
        return h;
    }
    return nullptr;
}
```

**为什么需要这个检查**：消费者可能在 `await_suspend` 之后、生产者 `yield_value` 之前被销毁（例如 `Coroutine<T>` 析构）。如果不检查，生产者会尝试 resume 已销毁的消费者 frame → UB。

#### 惰性求值与管道操作符

```cpp
auto pipeline = std::move(gen)
    | filter([](int x) { return x % 2 == 0; })
    | transform([](int x) { return x * 3; })
    | take(3);

auto result = co_await std::move(pipeline).collect<std::vector>();
```

**惰性求值原理**：每个操作符（`filterImpl`、`transformImpl` 等）返回一个新的 `AsyncGenerator`，内部 `co_await` 上游 generator 的 `next()`。**只有最外层被消费时，整个链才会被驱动**。

**生命周期保证**：下游的 `CoroutineAwaiter` 持有上游的 `CoroutineControlBlock`，形成引用链 `下游 → 上游 → ... → 源`。最后一个释放时整条链被析构。

### 3.7 AsyncIOOperation\<T\> —— 异步 IO 操作基类

**文件**：`src/base/coroutine/AsyncIOOperation.hpp`

**职责**：提供异步 IO 操作的通用框架，封装了：
- 状态管理（Pending → Completed / Failed / TimedOut）
- 超时处理
- 清理回调
- 双重检查竞态

#### 为什么需要 AsyncIOOperation？

直接使用 `FdReadyAwaiter` 或 `TimerAwaiter` 需要手动处理多个竞争条件：
1. 操作同步完成 vs 异步完成
2. 超时 vs 正常完成
3. 清理（如注销 fd 事件）

`AsyncIOOperation` 封装了这些复杂性，子类只需实现 `startOperation()`。

#### IOState

```cpp
template <typename T>
struct IOState {
    std::atomic<CompletionState> state{CompletionState::Pending};
    std::atomic<ResumePhase> resumePhase{ResumePhase::Registering};
    std::shared_ptr<SharedCoroutineHandle> resumeHandle;
    std::optional<T> result;
    std::optional<Error> error;
    std::function<void()> cleanup;
};
```

#### await_suspend 流程

```
await_suspend(handle)
  │
  ├─ state_->resumeHandle = handle.promise().makeResumeHandle()
  │    创建恢复令牌
  │
  ├─ scheduleTimeout()
  │    注册超时定时器（如果设置了 timeout）
  │
  ├─ startOperation()
  │    子类实现：可能同步完成，也可能注册 fd 事件后返回
  │
  ├─ state 是否为 Pending？
  │   ├─ NO → return false（不挂起，同步完成了）
  │   └─ YES → store(ResumePhase::Suspended)
  │
  ├─ 再次检查 state（双重检查）
  │   ├─ Pending → 正常挂起: return suspend()
  │   └─ 非 Pending → 竞态窗口内完成了:
  │       CAS(Resumed) → 返回 false（不挂起）
  │
  └─ return suspend() 结果
```

**双重检查的原因**：`startOperation()` 和 `scheduleTimeout()` 之间有竞态窗口。`startOperation()` 可能异步注册 fd 事件后返回（此时 state 还是 Pending），但 fd 可能在 `scheduleTimeout()` 之前就就绪了。如果不做双重检查，协程可能被挂起后又立即被 fd 事件恢复，导致状态不一致。

#### complete / fail / timeout 竞争

```cpp
void complete(T result) noexcept {
    auto expected = CompletionState::Pending;
    if (state_->state.compare_exchange_strong(expected, CompletionState::Completed, ...)) {
        state_->result.emplace(std::move(result));
        cancelTimer();          // 取消超时定时器
        finishState(*state_);   // 执行 cleanup + 调度 resume
    }
}
```

**CAS 保证 only-one-winner**：`complete`、`fail`、`timeout` 三个路径通过 CAS 竞争，只有第一个成功转换 `Pending → {Completed/Failed/TimedOut}` 的胜出。胜出者负责 `cancelTimer()` + `cleanup` + `scheduleResume()`。

### 3.8 TimerAwaiter / CompletionAwaiter —— 工具 Awaiter

**文件**：`src/base/coroutine/Awaitable.hpp`

#### TimerAwaiter

```cpp
class TimerAwaiter {
public:
    explicit TimerAwaiter(std::chrono::milliseconds duration) noexcept;

    bool await_ready() const noexcept { return duration_.count() <= 0; }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto shared = handle.promise().makeResumeHandle();
        if (!shared) return false;
        CoroutineScheduler::instance().scheduleCallback(duration_, [shared]() {
            if (shared->notify()) {
                CoroutineScheduler::instance().schedule(shared);
            }
        });
        return shared->suspend();
    }
};
```

**设计要点**：
- 零延迟时 `await_ready` 返回 `true`，不挂起——适用于"立即执行"的场景
- 通过 `makeResumeHandle()` 创建恢复令牌，定时器触发时 `notify()` + `schedule()`
- 与 `SharedCoroutineHandle` 的状态机配合：如果在 `scheduleCallback` 之后、`suspend()` 之前定时器触发，`notify()` 将状态设为 `Signaled`，`suspend()` 返回 `false`（不挂起）

#### CompletionAwaiter

```cpp
template <typename T>
class CompletionAwaiter {
public:
    bool await_ready() const noexcept {
        return state_.load() != State::Init;
    }

    template <typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto sharedHandle = handle.promise().makeResumeHandle();
        auto expected = State::Init;
        if (!state_.compare_exchange_strong(expected, State::Waiting, ...)) {
            return false;  // 已经在 await_ready 和 await_suspend 之间被 complete
        }
        handle_ = std::move(sharedHandle);
        return handle_ && handle_->suspend();
    }

    void complete(T result) noexcept {
        result_.emplace(std::move(result));
        finish();
    }

private:
    enum class State : uint8_t { Init, Waiting, Completed };

    void finish() noexcept {
        auto expected = State::Waiting;
        if (state_.compare_exchange_strong(expected, State::Completed, ...)) {
            if (handle_ && handle_->notify()) {
                CoroutineScheduler::instance().schedule(handle_);
            }
            return;
        }
        state_.store(State::Completed, ...);  // 让 await_suspend 看到 Completed，不挂起
    }
};
```

**设计要点**：解决 "complete 在 await_suspend 之前还是之后" 的竞态
- `complete()` 先于 `await_suspend` 调用：`finish()` 将 state 设为 `Completed`，`await_suspend` 看到 state != `Init` 返回 `false`（不挂起）
- `await_suspend` 先于 `complete()` 调用：state 从 `Init → Waiting`，`complete()` 触发 `finish()` → `notify()` → `schedule()` 恢复协程
- **用途**：将回调式 API 桥接到协程（如 DNS 解析 `getaddrinfo` 在线程池执行，完成后 `complete`）

### 3.9 CoroutineScheduler —— 协程调度器

**文件**：`src/base/coroutine/CoroutineScheduler.hpp`

**职责**：单例，负责协程的调度执行和定时任务管理。

```cpp
class CoroutineScheduler : public NonCopyable {
public:
    static CoroutineScheduler& instance() noexcept;

    // 立即调度协程在后台线程执行
    void schedule(std::shared_ptr<SharedCoroutineHandle> shared);
    void schedule(SharedCoroutineHandle handle);
    template <typename T> void schedule(Coroutine<T>& coroutine);

    // 延迟调度
    TimerId scheduleAfter(delay, SharedCoroutineHandle handle);

    // 纯定时回调（不涉及协程恢复）
    TimerId scheduleCallback(delay, callback);

    void cancel(TimerId timerId);
    void waitForAll() noexcept;
    void shutdown() noexcept;
};
```

#### 为什么是单例 + 固定 4 线程的线程池？

- **单例**：协程调度是全局基础设施，所有模块共享同一调度器。`CoroutineScheduler` 和 `EventLoop` 的单例保证调度有序。
- **4 线程**：IO 密集型工作，4 线程足够利用多核，过多线程反而增加上下文切换成本。`ThreadPool` 支持 job 优先级（High/Normal/Low）。

#### 调度链

```
co_await FdReadyAwaiter  →  EventLoop fd 就绪
                                  │
                                  ▼
                          dispatchEvent()
                                  │
                                  ▼
                          handle->notify()  (Registering → Signaled / Suspended → Scheduled)
                                  │ notify() 返回 true
                                  ▼
                          CoroutineScheduler::schedule(shared)
                                  │
                                  ▼
                          ThreadPool::postJob([shared] { shared->tryResume(); })
                                  │
                                  ▼
                          control_->resume() → handle_.resume()
```

### 3.10 EventLoop —— IO 事件循环

**文件**：`src/base/coroutine/EventLoop.h/.cpp`

**职责**：IO 多路复用封装（Linux epoll / macOS kqueue），驱动 fd 就绪事件的注册、分发、注销。

```cpp
class EventLoop : public NonCopyable {
public:
    void run() noexcept;   // 主循环：poll → dispatch
    void stop() noexcept;  // 停止循环
    void wakeup() noexcept;

    void registerFd(int32_t fd);
    void unregisterFd(int32_t fd);
    void setReadable(int32_t fd, bool enabled);
    void setWritable(int32_t fd, bool enabled);

    // 注册协程恢复令牌到 fd 事件
    RegistrationToken registerContinuation(
        int32_t fd, FdInterest interest,
        std::shared_ptr<SharedCoroutineHandle> handle);

    // RegistrationToken：RAII 句柄，析构时自动注销
    class RegistrationToken {
        void cancel() noexcept;  // 注销 + 恢复协程
    };
};
```

#### 设计决策

**Q: 为什么 `continuations_` 用 `mutex` 保护？**
- EventLoop 线程执行 `dispatchEvent()`，外部线程（如 cancel 回调）执行 `unregisterContinuation()`
- 两者可能并发操作同一 fd 的 `FdWaiters`，必须加锁
- 但锁持有时间极短（仅查找和移动 shared_ptr），不是性能瓶颈

**Q: 为什么 `RegistrationToken::cancel()` 可以恢复协程？**
- `cancel()` 从 EventLoop 摘除续期，然后 `notify()` + `schedule()` 恢复协程
- 这允许 `CancellationToken` 的取消路径也恢复协程，与 fd 就绪路径竞争，通过 `resumed_` CAS 保证只恢复一次

### 3.11 CancellationToken —— 取消传播

**文件**：`src/base/coroutine/CancellationToken.h`

**职责**：提供协程取消信号传播机制。

```cpp
class CancellationToken {
    struct State {
        std::atomic<bool> cancelled{false};
        std::mutex mutex;
        std::unordered_map<TokenId, Callback> callbackMap;
        TokenId nextTokenId{0};
    };
    std::shared_ptr<State> state_;
};
```

#### 设计决策

**Q: 为什么 `shared_ptr<State>` 共享状态？**
- 一个 token 的取消应对所有拷贝生效
- 例如：`HttpRequest` 有一个 `CancellationToken`，`HttpStream` 通过 `req->cancelToken()` 获取引用
- 调用 `request_->cancel()` 时，所有正在运行的协程（包括通过 `streamingExecute` 创建的 `AsyncGenerator` 消费者）都应被取消

**Q: 为什么锁外执行回调？**
- `cancel()` 在锁内提取所有回调，在锁外执行
- 防止死锁：回调中可能反调 `registerCallback()`（虽然实际使用中很少这样做）
- 也减少锁持有时间，避免影响 `registerCallback()` 的并发性能

**Q: 为什么已取消时注册返回无效句柄？**
- 避免"注册后立即触发"的复杂时序
- 如果 token 已取消，新注册的回调不会被触发，调用方检查 `registration_.isValid()` 后决定是否挂起协程

---

## 4. 模块间协作

### 4.1 模块关系图

```
                    Coroutine<T>
                        │
                        │ 创建/持有
                        ▼
              CoroutineControlBlock
                        │
                        │ shared_ptr 引用
                        │
        ┌───────────────┼───────────────┐
        │               │               │
        ▼               ▼               ▼
  CoroutineAwaiter  SharedCoroutine  AsyncGenerator
  (co_await 嵌套)     Handle         (生产者)
                        │
                        │ 被 EventLoop / Timer / CancellationToken 持有
                        │
              ┌─────────┴─────────┐
              │                   │
              ▼                   ▼
         EventLoop          CoroutineScheduler
         (IO 多路复用)         (线程池 + 定时器)
              │
              │ 通过 IOContext 暴露
              ▼
         AsyncConnect / AsyncRead / AsyncWrite / FdReadyAwaiter
              │
              │ 继承自
              ▼
         AsyncIOOperation
              │
              │ 使用
              ▼
         PromiseBase::makeResumeHandle()
```

### 4.2 典型数据流

```
用户代码:
  Coroutine<T> coro = someTask();
  coro.resume();          // 恢复协程执行

协程内部:
  auto result = co_await asyncConnect(fd, host, port);
      │
      ▼
  AsyncIOOperation::await_suspend(handle)
      ├─ handle.promise().makeResumeHandle()
      │    → control_.lock() → shared_ptr<CoroutineControlBlock>
      │    → make_shared<SharedCoroutineHandle>(control)
      │
      ├─ scheduleTimeout()
      │    → CoroutineScheduler::scheduleCallback(timeout, cb)
      │
      ├─ startOperation()
      │    → ::connect() → EINPROGRESS
      │    → setCleanup([fd]{ unregisterContinuation(fd); })
      │    → IOContext::registerContinuation(fd, Write, resumeHandle)
      │    → IOContext::setWritable(fd, true)
      │
      └─ suspend() → Registering → Suspended (挂起)

  [fd 就绪]:
  EventLoop::dispatchEvent(fd, writable)
      → takeContinuation(fd, Write)
      → handle->notify() → Suspended → Scheduled → return true
      → CoroutineScheduler::schedule(handle)
      → ThreadPool::postJob([handle]{ handle->tryResume(); })
      → resumed_ CAS → control_->resume() → handle_.resume()
      → await_resume() → getsockopt(SO_ERROR) → 校验成功
```

### 4.3 竞态场景与防御

| 竞态场景 | 涉及模块 | 防御机制 |
|----------|----------|----------|
| notify 在 suspend 前到达 | SharedCoroutineHandle + EventLoop | `State` 状态机：Registering → Signaled |
| 多个完成源同时 notify | SharedCoroutineHandle | `resumed_` 原子 CAS |
| complete/fail/timeout 竞争 | AsyncIOOperation | `CompletionState` CAS |
| complete 在 await_suspend 之前/之后 | CompletionAwaiter | `State` CAS（Init/Waiting/Completed） |
| 消费者在生产者 yield 前销毁 | AsyncGenerator | `consumerControl_.lock()` 检查 |
| Coroutine<T> 析构但 frame 还在运行 | CoroutineControlBlock | shared_ptr 引用计数 |
| 父协程在子协程完成前销毁 | CoroutineAwaiter | makeResumeHandle 的 weak_ptr lock |
| cancel 回调与 fd 就绪同时触发 | CancellationToken + EventLoop | `resumed_` CAS + RegistrationToken::cancel |
| resume 在 done 之后调用 | CoroutineControlBlock | `isDone()` 检查 |

---

## 5. 核心设计决策

### 5.1 为什么选择 `CoroutineControlBlock` 而不是其他方案？

| 方案 | 问题 | 结论 |
|------|------|------|
| `shared_ptr<coroutine_handle>` | `coroutine_handle` 是移动-only，不能放 `shared_ptr` | ❌ |
| `enable_shared_from_this` + promise | Promise 是 frame 子对象，不能独立于 frame 存活 | ❌ |
| 全局 Registry | 全局 mutex 性能差，无法消除 UAF 窗口 | ❌ |
| `shared_ptr<Promise>` aliasing constructor | 只能构造"指向但不拥有"的指针，仍需额外机制触发 destroy | ❌ |
| **`CoroutineControlBlock`** | 独立于 frame，shared_ptr 管理，析构时 destroy | ✅ |

### 5.2 为什么 SharedCoroutineHandle 需要两个原子变量？

- `state_` 解决 await_suspend 内部的"通知-挂起"时序竞态
- `resumed_` 解决 await_suspend 之后的"多源 resume"竞态
- 两者不能合并：因为它们保护不同时间窗口内的竞争

### 5.3 为什么 PromiseBase 用 `weak_ptr<CoroutineControlBlock>` 而非 `shared_ptr`？

- 避免循环引用：Promise（frame 的子对象）和 CoroutineControlBlock 互相持有 shared_ptr → 内存泄漏
- `weak_ptr` 打破循环：promise 只在需要时 `lock()` 临时提升为 shared_ptr

### 5.4 为什么 `Coroutine<T>::~Coroutine()` 是 `= default`？

- 这是"统一所有权"原则的核心体现
- 如果 `~Coroutine()` 调 `handle.destroy()`，则在 `Coroutine<T>` 析构时 frame 被强制销毁
- 但此时 `SharedCoroutineHandle` 可能还持有 `CoroutineControlBlock`，继续使用已销毁的 frame → UAF
- `= default` 只释放一个 `shared_ptr` 引用，frame 在最后一个引用释放时才销毁

### 5.5 为什么 AsyncIOOperation 需要双重检查？

```
线程 A: await_suspend              线程 B: complete
  │                                   │
  ├─ startOperation()                 │
  │   └─ 注册 fd 事件                 │
  ├─ [fd 就绪] ◄───────────────  ├─ fd 就绪
  │   └─ notify() → state=Signaled   │
  ├─ 检查 state = Signaled            │
  │   (不 Pending，返回 false)        │
  └─ 不挂起 ✓                        │
```

如果没有双重检查，时序可能为：
```
1. startOperation() 注册 fd 事件
2. fd 就绪 → notify() → state = Completed
3. store(ResumePhase::Suspended)  ← 在 complete 之后设置 Suspended
4. suspend() → 挂起（但协程已完成！）
5. 协程永久挂起（死锁）
```

双重检查确保：在标记 `Suspended` 之前和之后都检查 state，如果 state 已经不再是 `Pending`，则不挂起。

### 5.6 为什么 AsyncGenerator 需要 `weak_ptr<CoroutineControlBlock>` consumer 检查？

防止消费者在 await_suspend 之后、生产者 yield_value 之前被销毁的场景：

```
1. 消费者 co_await gen.next()
2. await_suspend: 消费者注册到生产者 promise
3. 消费者 Coroutine<T> 析构 → CoroutineControlBlock 引用归零 → frame 销毁
4. 生产者 co_yield value
5. takeWaitingConsumer() 取到消费者 handle
6. consumer.resume() → 访问已销毁的 frame → UB
```

`consumerControl_.lock()` 检查打破了这个链条：如果消费者 frame 已销毁，返回 `nullptr`，生产者不恢复消费者。

### 5.7 为什么 HLSStream 用 `vector<Coroutine<void>>` 而非单个成员？

- HLS 流媒体涉及多个递归协程（master → variant → segment），每个完成后可能启动下一个
- 如果用 `Coroutine<void>` 单个成员，`operator=` 移动赋值时旧 `Coroutine` 析构 → `control_.reset()` → 如果旧 frame 还在运行中则 UB
- `vector` 只追加存储，插入时清理已完成的协程，不影响正在运行的

### 5.8 性能取舍

| 决策 | 收益 | 成本 |
|------|------|------|
| shared_ptr 引用计数 | 消除 UAF | 原子操作开销（< 10ns/op） |
| CoroutineControlBlock::resumeMutex_ | resume 线程安全 | 极低（直接调用路径极少） |
| AsyncIOOperation 双重检查 | 消除永久挂起 | 多一次 atomic load |
| PromiseBase 统一入口 | 消除重复代码 | 多一层 weak_ptr lock |
| 全局单例 CoroutineScheduler | 调度有序 | 测试间状态污染（需清理） |

---

## 6. 实际使用模式

### 6.1 基本协程

```cpp
auto coro = []() -> Coroutine<int> {
    co_return 42;
}();
coro.resume();
int result = coro.getResult();  // 42
```

### 6.2 协程嵌套

```cpp
auto outer = []() -> Coroutine<int> {
    auto inner = []() -> Coroutine<int> {
        co_return 1;
    }();
    int v = co_await std::move(inner);  // 右值 co_await
    co_return v + 100;
}();

// 也支持左值 co_await（新方案支持）
auto inner = []() -> Coroutine<int> { co_return 1; }();
int v = co_await inner;  // 左值也可以！
```

### 6.3 异步 IO

```cpp
class AsyncConnect : public AsyncIOOperation<void> {
protected:
    void startOperation() noexcept override {
        int32_t result = ::connect(fd_, addr, addrLen);
        if (result == 0) { complete(); }
        else if (errno == EINPROGRESS) {
            setCleanup([fd]() {
                IOContext::instance().unregisterContinuation(fd, FdInterest::Write);
            });
            IOContext::instance().registerContinuation(fd_, FdInterest::Write, getSharedHandle());
            IOContext::instance().setWritable(fd_, true);
        } else { fail(Error::ConnectionRefused); }
    }
};

// 使用
auto result = co_await AsyncConnect(fd, host, port).timeout(5s);
```

### 6.4 流式 HTTP 响应

```cpp
auto streamTask = [](auto req, auto info, auto weakStream) -> Coroutine<void> {
    auto gen = req->streamingExecute(info, req->cancelToken());
    while (true) {
        auto chunk = co_await gen.next();
        if (!chunk.has_value()) break;
        if (auto stream = weakStream.lock()) {
            stream->handleChunk(std::move(chunk.value()));
        }
    }
};
streamCoroutine_ = streamTask(request_, requestInfo, weakThis);
CoroutineScheduler::instance().schedule(streamCoroutine_);
```

### 6.5 带超时和取消的 fd 等待

```cpp
// CancellableFdReadyAwaiter 结合 CancellationToken 和 EventLoop
bool ready = co_await CancellableFdReadyAwaiter(fd, Interest::Read, cancelToken);
if (ready) {
    // 正常读取
} else {
    // 被取消
}
```

### 6.6 回调桥接

```cpp
// 将回调式 API（如 getaddrinfo）包装为协程
Coroutine<AddressInfoPtr> resolveAsync(std::string host) {
    auto awaiter = std::make_shared<CompletionAwaiter<AddressInfoPtr>>();
    threadPool_.submit([host, awaiter]() {
        auto result = getaddrinfo(host.c_str(), ...);
        awaiter->complete(makeAddressInfo(result));
    });
    AddressInfoPtr resolved = co_await *awaiter;
    co_return resolved;
}
```

### 6.7 管道操作符

```cpp
auto pipeline = std::move(gen)
    | filter([](int x) { return x % 2 == 0; })
    | transform([](int x) { return x * 3; })
    | take(3);
auto result = co_await std::move(pipeline).collect<std::vector>();
```

---

## 7. 测试策略

### 7.1 测试覆盖层次

| 层次 | 测试文件 | 覆盖 |
|------|----------|------|
| 单元测试 | `test/base/CoroutineTest.cpp` | 每个模块的基本功能 |
| 集成测试 | `test/base/EventLoopTest.cpp` | 模块间协作 |
| 场景测试 | `test/base/CoroutineTest.cpp`（生命周期部分） | 竞态场景 |
| 端到端测试 | `test/http/HttpModuleTest.cpp` | 完整 HTTP 请求 |

### 7.2 关键测试场景

| 场景 | 代码 |
|------|------|
| Coroutine 析构后 resume | `DestroyedCoroutineHandleNotResumedByCancellation` |
| 嵌套临时协程生命周期 | `NestedTemporaryCoroutinesKeepIntermediateFramesAlive` |
| 生成器 owner 销毁后 frame 存活 | `DestroyedGeneratorOwnerDoesNotInvalidatePendingFrame` |
| at-most-once resume | `ResumeAlreadyDone` |
| share() 转移所有权 | `Share` |
| CompletionAwaiter 竞态 | `CompleteBeforeAwait` / `CompleteAfterAwait` |
| 多层 co_await | `CoAwaitDeepChain` |

### 7.3 全局状态清理

由于 `CoroutineScheduler` 和 `TimerManager` 是单例，测试间需要清理：

```cpp
void cleanupGlobalState() {
    for (int i = 0; i < 10; ++i) {
        CoroutineScheduler::instance().waitForAll();
        TimerManager::shareInstance().clear();
        CoroutineScheduler::instance().waitForAll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
```

---

## 8. 改造历史

| 日期 | 变更 |
|------|------|
| 2026-07-16 | 初始设计文档（validFlag → 统一所有权方案） |
| 2026-07-20 | 实际实现：`CoroutineControlBlock`、`SharedCoroutineHandle` 状态机、`PromiseBase` 统一入口 |
| 2026-07-21 | 修复 `AsyncIOOperation` 双重检查竞态、补充测试、更新为框架设计文档 |

### 文件清单

| 文件 | 说明 |
|------|------|
| `src/base/coroutine/CoroutineControlBlock.hpp` | **新增**：生命周期仲裁者 |
| `src/base/coroutine/Coroutine.hpp` | 重写：`shared_ptr<CoroutineControlBlock>`；`share()`；左值 `co_await` |
| `src/base/coroutine/SharedCoroutineHandle.hpp` | 重写：`State` 状态机；`resumed_` CAS |
| `src/base/coroutine/AsyncIOOperation.hpp` | 重写：`ResumePhase` 双重检查；`makeResumeHandle()` 统一入口 |
| `src/base/coroutine/Awaitable.hpp` | 适配：TimerAwaiter/CompletionAwaiter 改用 `makeResumeHandle()` |
| `src/base/coroutine/AsyncGenerator.hpp` | 适配：`weak_ptr` consumer 有效性检查 |
| `src/base/coroutine/CoroutineScheduler.hpp` | 无变化 |
| `src/base/coroutine/EventLoop.h/.cpp` | 无变化（API 一致） |
| `src/base/coroutine/CancellationToken.h` | 无变化 |
| `src/http/IOContext.h` | 适配：FdReadyAwaiter/CancellableFdReadyAwaiter 改用 `makeResumeHandle()` |
| `test/base/CoroutineTest.cpp` | 新增生命周期/竞态测试 |
| **删除** `CoroutineHandleRegistry.hpp` | 不再需要 |
