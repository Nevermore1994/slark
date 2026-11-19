# Slark C++20 协程框架设计

> 状态：已实现
> 更新：2026-11-18
> 第 2–7 节是重构前的历史设计：`CoroutineControlBlock`、`SharedCoroutineHandle`、`Coroutine<T>`、`resume()` / `getResult()` 以及 §5 的 `resumeMutex_` 均已删除；第 2 节模块清单、第 6 节示例代码、第 7 节测试名都不可照抄。当前 API、契约与测试映射以第 8 节为准，闭环总表见 §8.17。
> 作者：Nevermore-
> 关联代码：`src/base/coroutine/`、`src/http/IOContext.h`、`src/core/stream/HttpStream*`、`src/core/stream/HLSStream*`

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
8. [当前实现契约补充](#8-当前实现契约补充)

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
    [[nodiscard]] bool isCompleted() const noexcept;   // 轮询，不挂起

    // 每次 co_await 产生一个 Waiter，拥有本次等待的 gate 与取消注册
    class Waiter {
    public:
        bool await_ready() const noexcept;             // 已完成则不挂起
        template <typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) noexcept;
        T await_resume();                              // 取消抛 CancelledException，失败抛原异常
    private:
        std::shared_ptr<State> state_;
        std::shared_ptr<ResumeGate> gate_;
        std::list<std::shared_ptr<ResumeGate>>::iterator position_;
        CancellationTokenRegistration cancelReg_;
    };

    Waiter operator co_await() noexcept;

    void complete(T value);      // 仅首次生效
    void complete();             // void 版本
    void fail(std::exception_ptr e);

private:
    struct State {
        std::mutex mutex;
        bool completed = false;
        std::optional<ResultType> result;
        std::exception_ptr exception;
        std::list<std::shared_ptr<ResumeGate>> waiters; // 尚未结束等待的等待者
    };
    std::shared_ptr<State> state_;
};
```

**设计要点**：
- **共享状态**：completer 与所有等待者共享同一个 `State`（结果、完成标记、仍挂起的等待者列表）。
- **每次等待一个 Waiter**：挂起状态（gate、取消注册、列表位置）放在 Waiter 上，多个协程同时等待互不覆盖。
- **挂起**：在状态锁内完成入列、绑定取消并 `arm()`；若已完成则直接返回，不挂起。
- **唤醒**：`complete()` / `fail()` 在锁内发布结果并快照等待者列表，锁外逐个 `signal()`；只有第一次生效。
- **销毁**：`Waiter` 析构时在锁内从列表摘除自己并 `disarm()`，帧销毁后不会被唤醒。
- **结果**：`T` 必须可拷贝（`requires` 编译期约束），每个等待者拿到一份拷贝，与 Kotlin 的 `Deferred` 共享同一结果一致；只能移动的值请用 `std::shared_ptr` 包装。生产者析构时仍挂起的等待者被 `disarm`，不再被唤醒。
- **用途**：将回调式 API 桥接到协程（如 DNS 解析 `getaddrinfo` 在线程池执行，完成后 `complete`），多个协程可等待同一完成事件。

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
- 例如：`HttpRequest` 为每次操作创建独立的取消状态，`streamingExecute` 默认绑定该状态
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
| complete 在 await_suspend 之前/之后 | CompletionAwaiter | 共享 `State` 的互斥锁：已完成则不挂起，否则入列后由 `complete` 唤醒 |
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

> 历史章节：以下 `Coroutine<int>` / `resume()` / `getResult()` 示例对应的 API 已删除，当前用法见第 8 节。

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
    auto gen = req->streamingExecute(info);
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
// 读等待受截止时间约束；协程被取消（例如外层 with_timeout 超时）时抛出 CancelledException。
bool ready = co_await FdReadyAwaiter(fd, FdReadyAwaiter::Interest::Read, std::chrono::milliseconds{5000});
if (ready) {
    // 正常读取
} else {
    // 截止时间已到，或 fd 已注销
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
| CompletionAwaiter 竞态 | `CompletionBeforeAwaitSuspensionIsConsumed` / `CompletionAfterSuspensionResumesExactlyOnce` / `CompleteRacingRegistrationWakesEveryWaiter`，多等待者：`MultipleWaitersAreAllWokenWithTheValue` 等 |
| 多层 co_await | `CoAwaitDeepChain` |
| 执行器拒绝后的续体 | `RefusedResumeIsCancelledAndUnwindsOnFallback` / `RefusedResumeDestroysLocalsOffTheCallerThread` / `RefusedResumeOnStoppedThreadSchedulerUnwindsOnFallback`；`ThreadSchedulerTest.TryExecuteRefusesAfterStop` |
| 停止时已接受的任务 | `ThreadSchedulerTest.StopRunsEveryAcceptedTaskBeforeReturning` / `WorkPostedByADrainedTaskIsRefused` / `StopCancelsTimerWhoseCallbackIsQueuedBehindWork`；`ThreadBaseTest.StopRunsQueuedTasksAndReleasesCaptures`；`EngineCommandTest.CloseTurnsCommandsQueuedBeforeItIntoNoOps` |
| AsyncIO 完成竞态与结果可见 | `AsyncIOOperationTest.CompleteRacingFailHasExactlyOneOutcome` / `CompleteRacingTimeoutHasExactlyOneOutcome` / `ResultCompletedOnAnotherThreadIsReadByTheWaiter` / `ErrorCompletedOnAnotherThreadIsReadByTheWaiter` |
| 嵌套协程执行域继承 | `ExecutorTest.ChildWithoutExecutorResumesOnTheRootExecutorThroughTwoLevels` / `ChildKeepsItsOwnExecutorOverTheAwaitingParentsDomain` |
| FdReady 取消、超时、注销与执行域 | `IOContextTest.CancellingTheCoroutineWakesAPendingFdReadyWait` / `TimeoutWakesAPendingFdReadyWait` / `UnregisteringTheSocketReportsAPendingWaitAsNotReady` / `ReadinessResumesTheCoroutineOnItsBoundExecutor`；显式令牌版本为 `CancellingTheTokenWakesAPendingFdReadyWait` |
| 协程压力 | `CoroutineStressTest.*`：100 协程并发恢复、100 定时器并发触发、100 个 AsyncIO 并发完成、50 层嵌套 co_await、10 个 when_all 串行 |
| 恢复派发计数 | `ResumeMetricsTest.*`：交给执行域、内联、被拒绝回退、排队延迟、pending、`ResumeOfAFrameThatIsStillRunningIsRefusedAndCounted`（冲突，见 8.2、8.16） |
| 竞态（完成、失败、取消、派发） | `AsyncIOOperationTest.FailRacingCancelHasExactlyOneOutcome`；`IOContextTest.ReadinessRacingCancelResumesExactlyOnce`；`StructuredConcurrencyTest.FailureRacingCompletionRethrowsOnlyAfterEveryChildFinishes`；`CancellationPropagationTest.AsyncScopeSpawnRacingCancelAllNeverRunsRejectedChildren` |
| 令牌注册窗口 | `AsyncIOOperationTest.CancelRacingTheSuspensionNeverLosesTheWake`（改回两步写法后丢失取消，突变验证）；`CancellationPropagationTest.RegisterOrRunRunsTheCallbackImmediatelyWhenAlreadyCancelled` / `RegisterOrRunKeepsTheCallbackUntilCancelled` |
| 启动后绑定与执行域冻结 | `CancellationPropagationTest.SetCancellationTokenAfterFirstResumeThrows`；`ExecutorTest.SetExecutorAfterFirstResumeThrows`；`ExecutorTest.SetExecutorAfterCompletionThrows`；`ExecutorTest.SetExecutorNullAfterFirstResumeThrows`；`AsyncGeneratorTest.SetExecutorAfterFirstNextThrows`；`AsyncGeneratorTest.SetExecutorNullAfterFirstNextThrows`；`AsyncGeneratorTest.SetExecutorAfterCompletionThrows`；`CoScopeTest.SetDefaultExecutorAfterSpawnThrows` |
| 启动前绑定与清除 | `ExecutorTest.SetExecutorNullBeforeStartClearsTheDomain`；`AsyncGeneratorTest.SetExecutorBeforeFirstNextRunsTheProducerOnItsDomain`；`AsyncGeneratorTest.SetExecutorNullBeforeFirstNextClearsTheDomain`；`CoScopeTest.SetDefaultExecutorReplacesTheDomainForLaterChildren` |
| 空、关闭与移后对象的绑定 | `CoroutineBoundaryTest.BindingsOnAnEmptyTaskAreNoOps`；`CoroutineBoundaryTest.MovedFromTaskIgnoresLaterBindings`；`CoroutineBoundaryTest.MovedStartedTaskStillRejectsBindings`；`AsyncGeneratorTest.SetExecutorAfterCloseIsANoOp`；`AsyncGeneratorTest.SetExecutorOnAMovedFromOrEmptyGeneratorIsANoOp` |
| `co_await` 已启动的子协程 | `CoroutineBoundaryTest.CoAwaitOfAStartedSuspendedTaskIsRejectedWithoutResumingIt`；`CoroutineBoundaryTest.CoAwaitOfAStartedTaskThatAlreadyCompletedReturnsItsResult` |
| scope 执行器被拒绝派发 | `CoScopeTest.RefusedSpawnReturnsFalseAndNeverRuns`；`CoScopeTest.RefusedSpawnWithExplicitTokenReturnsFalseAndNeverRuns`；`CoScopeTest.RefusedSpawnDoesNotCancelTheScope` |
| scope 执行器的构造与生存期 | `CoScopeTest.ConstructorExecutorRunsChildrenOnItsDomain`；`CoScopeTest.ConstructorWithNullExecutorRunsChildrenInline`；`CoScopeTest.ConstructorExecutorOutlivesTheScopeReference` |
| 执行器归属（8.12） | 拥有型：`CoScopeTest.SetDefaultExecutorOutlivesTheCallersReferenceWhileAChildIsParked`（子任务挂起时调用方释放引用）；`StructuredConcurrencyTest.WhenAllOwnsItsDomainUntilTheStoredAwaiterRuns`（先存放 awaiter 再 `co_await`）；`CoWhenAllTest.WhenAllWithExecutor`。弱引用：`CoScopeTest.ConstructorWeakExecutorRunsChildrenOnItsDomain`、`CoScopeTest.WeakDomainDoesNotKeepTheExecutorAliveAndRefusesLaterChildren`、`CoScopeTest.WeakDomainKeepsAnAcceptedDispatchAliveUntilItRuns`、`CoScopeTest.WeakDomainRefusesAParkedChildOnceTheExecutorIsGone`、`StructuredConcurrencyTest.WhenAllWeakDomainDoesNotKeepTheExecutorAlive`、`StructuredConcurrencyTest.WhenAllWithAnEmptyWeakDomainRefusesItsChildren`；`StructuredConcurrencyTest.ScopeDestroyedOnItsOwnExecutorDoesNotDeadlock` 的域改为弱引用。突变验证：弱引用改为拥有型、过期时内联执行、派发闭包不持有执行器，三者各自只使对应用例变红 |
| scope 令牌与显式令牌合并 | `CoScopeTest.SpawnWithExplicitTokenIsCancelledByScope`；`CoScopeTest.SpawnWithExplicitTokenIsCancelledByItsOwnToken` |
| 令牌 link 与子令牌的生命周期 | `CancellationPropagationTest.LinkedTokenKeepsItsSourcesAlive`；`CancellationPropagationTest.WhenAnyChildrenInheritParentToken`；`CancellationPropagationTest.ChildOfIsCancelledByItsParent` / `ChildOfNeverCancelsItsParent` / `ChildOfACancelledParentIsBornCancelled` / `ChildOfNothingIsAnIndependentToken` / `ChildOfKeepsATemporaryParentChainAlive` / `ChildOfRegistersOnceAndLeavesWhenTheChildIsReleased` |
| 令牌契约套件（8.13） | `TokenContractTest/<入口>_<叶子>.*`：到达、预取消（不挂起、立即展开）、隔离、有界、无残留（7 个入口 × 2 种叶子 Timer/Io × 5 条；`Await`、`Generator`、`WithTimeout` 没有自己的作用域，隔离一条跳过）。守卫：`TokenEdgeGuardTest.*`、`TokenEdgeGuardDeathTest.*`；scope 子令牌：`CoScopeTest.SpawnedChildHoldsItsOwnTokenSoCancellingItDoesNotCancelTheScope`。突变验证：撤销 await 边、撤销叶子登记、撤销 `when_all` 的挂接（守卫终止进程）、去掉 Job 体守卫、把 `spawn` 改回别名，各自只使对应用例变红 |
| 嵌套协程链销毁（8.3） | `ChainDestroyTest.DestroyingTheRootOfANestedChainReleasesEveryFrame`、`ChainDestroyTest.ADestroyedParentNeverRunsAfterItsChildFinishes`、`ChainDestroyTest.ClosingAGeneratorThatAwaitsAParkedChildReleasesTheChain`、`ChainDestroyTest.DestroyRacingChildCompletionReleasesEveryFrame`（修复前挂起的子协程拖住父协程帧） |
| 组合子在生成器体内（8.13） | `StructuredConcurrencyTest.WhenAllRunsInsideAGeneratorBody`、`WhenAnyRunsInsideAGeneratorBody`、`ClosingAGeneratorParkedInWhenAllCancelsItsChildren`（修复前读取 `Task` 专有的 `executor_`，编译失败） |
| Job 分离体的释放（8.18） | `JobTest.ADetachedBodyReleasesItsFramesWhenItEnds`、`JobTest.ARefusedJobReleasesItsBodyWhenDestroyed`、`JobTest.DestroyRacingTheBodyUnwindingOnItsExecutorReleasesEveryFrame` |
| 组合子子任务挂起后仍留在执行域（8.12） | `CoScopeTest.ConstructorExecutorKeepsChildrenOnItsDomainAcrossASuspension`、`CoScopeTest.DefaultExecutorKeepsChildrenOnItsDomainAcrossASuspension`、`CoScopeTest.NestedAwaitsInAChildStayOnTheScopeDomain`、`StructuredConcurrencyTest.WhenAllChildrenStayOnTheExplicitDomainAcrossASuspension`、`StructuredConcurrencyTest.WhenAllChildrenStayOnTheAwaiterDomainAcrossASuspension`、`WhenAnyTest.ChildrenStayOnTheScopeDomainAcrossASuspension`（修复前挂起后在定时器线程恢复）；守护用例 `CoScopeTest.ChildWithItsOwnDomainKeepsItAcrossASuspension`、`JobTest.ExecutorKeepsTheBodyOnItsDomainAcrossASuspension` |
| fd 同方向单等待者（8.9） | `EventLoopRaceTest.SecondWaiterOnTheSameFdAndDirectionEndsTheFirstAsNotReady`（突变验证：去掉替换时的终态通知后变红）；`EventLoopRaceTest.ReadAndWriteWaitersOnTheSameFdAreIndependent` |
| spawn 拒绝空 Task 与已启动 Task | `CoScopeTest.SpawnOfAnEmptyTaskThrowsAndRegistersNothing`；`CoScopeTest.SpawnOfAnEmptyTaskWithExplicitTokenThrowsAndRegistersNothing`；`CoScopeTest.SpawnOfAStartedSuspendedTaskThrowsAndNeverResumesIt`；`CoScopeTest.SpawnOfACompletedTaskThrows` |
| 生成器令牌 link 与启动后绑定（8.13） | `AsyncGeneratorTest.ProducerTokenIsLinkedWithTheConsumerTokenOnFirstNext`（突变验证：还原旧规则后变红）；`AsyncGeneratorTest.SetCancellationTokenAfterFirstNextThrows`；`CancellationPropagationTest.CancellationRequestedReadsTheFrameTokenWithoutSuspending` |
| 生成器令牌跟随当前消费者（8.13） | `CancellationPropagationTest.GeneratorLinksTheConsumerTokenOnlyOnce`、`CancellationPropagationTest.GeneratorWithItsOwnTokenLinksTheConsumerTokenOnlyOnce`（修复前链深 63、回调数 64，修复后不随 `next()` 次数增长）；交接：`CancellationPropagationTest.GeneratorHandoffFollowsTheCurrentConsumer`（修复前前一消费者的取消仍穿透到生产者）、`CancellationPropagationTest.GeneratorHandoffToATokenlessConsumerDropsThePreviousOne`、`CancellationPropagationTest.GeneratorHandoffsFollowEachConsumerAndStayBounded`（突变验证：改回叠加后变红）；守护用例 `CancellationPropagationTest.ConsumerCancellationReachesTheProducerAfterManyNexts` |
| 生成器单消费者（8.6） | `AsyncGeneratorTest.SecondPendingNextIsRejectedWithoutDrivingTheProducer`、`AsyncGeneratorTest.NextIsRejectedWhileTheProducerServesADestroyedConsumer`（突变验证：关闭检查后两者都变红） |
| 跨线程 `isDone()`（8.2） | `TaskTest.IsDoneFromAnotherThreadDoesNotWaitForTheRunningBody`（突变验证：改回持锁实现后因超时变红）；`TaskTest.DestroyingTheTaskRightAfterIsDoneIsRaceFree`；`TaskTest.IsDoneCanBeObservedWhileAnotherThreadCompletes` |
| FdReady 截止时间（8.20） | `SuspensionMatrixTest.FdReadyDeadlineEndsTheWaitAsNotReady`；`SuspensionMatrixTest.ReadinessBeforeDeadlineCancelsTheDeadline` |
| Job 生命周期（8.18） | `JobTest.JoinReturnsNormallyWhenTheBodySucceeds`、`JobTest.FailureIsRethrownByResultButNotByJoin`、`JobTest.CancelWakesTheBodyAndSettlesItAsCancelled`、`JobTest.WaitForIsWokenByCompletionOnAnotherThread`、`JobTest.DestroyingAStartedJobCancelsItsBodyWithoutWaiting`、`JobTest.DestroyingAnUnstartedJobNeverRunsItAndReleasesItsJoiners`、`JobTest.MoveAssignmentCancelsThePreviousJob`、`JobTest.ExecutorRunsTheBodyOnItsDomain`、`JobTest.RefusedExecutorSettlesTheJobAsCancelledAndNeverRunsIt`、`JobTest.CompletionRacingCancelSettlesExactlyOnce`、`JobTest.DestroyRacingCompletionNeverTouchesAFreedFrame` |
| 挂起点矩阵，基础层（8.19） | `SuspensionMatrixTest.TimerPreCancelEndsCancelledWithoutSuspending`、`SuspensionMatrixTest.TimerDestroyedWhileSuspendedNeverResumes`、`SuspensionMatrixTest.TimerRefusedDispatchEndsCancelled`、`SuspensionMatrixTest.YieldCancelRacingItsTickResolvesToExactlyOneOutcome`、`SuspensionMatrixTest.CompletionMidCancelEndsCancelledAndLaterCompletionIsInert`、`SuspensionMatrixTest.CompletionAlreadyCompletedDoesNotSuspend`、`SuspensionMatrixTest.CompletionRefusedDispatchEndsCancelled`、`SuspensionMatrixTest.JoinMidCancelEndsCancelledAndLeavesTheJobRunning`、`SuspensionMatrixTest.JoinReleasedByDestroyingTheJobEndsNormally`、`SuspensionMatrixTest.IoCompletedInStartDoesNotSuspend`、`SuspensionMatrixTest.IoRefusedDispatchEndsCancelled` |
| 挂起点矩阵，fd 层（8.19） | `SuspensionMatrixTest.FdReadyMidCancelEndsCancelled`、`SuspensionMatrixTest.FdReadyUnregisteredReportsNotReadyWithoutThrowing`、`SuspensionMatrixTest.FdReadyRefusedDispatchEndsCancelled`、`SuspensionMatrixTest.ReadPreCancelEndsCancelledWithoutReading`、`SuspensionMatrixTest.ReadWokenByReadinessRetriesAndReturnsTheData`、`SuspensionMatrixTest.ReadUnregisteredReportsSocketClosed`、`SuspensionMatrixTest.ReadDestroyedWhileSuspendedLeavesLaterDataUnread`、`SuspensionMatrixTest.WriteWokenByWritabilityRetriesAndReturnsTheCount`、`SuspensionMatrixTest.WriteUnregisteredReportsSocketClosed` |
| 超时边界 | `CoTimeoutTest.ZeroTimeoutExpiresBeforeTheChildCompletes`；调用方令牌绑在子任务上（8.14）：`CoTimeoutTest.TokenBoundToTheChildItselfStillCancelsIt`（突变验证：覆盖子任务令牌后变红） |
| 在途协程析构 | `HttpStreamTest.DestroyDuringTransferReleasesTheStream`；`HLSStreamTest.DestroyWhileDownloadingReleasesTheStream` |
| HLS 关闭与释放 | `HLSStreamTest.CloseCancelsThePlaylistRetryTimer` / `StreamIsReleasedOnceClosedWithoutAReferenceCycle` |

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

## 8. 当前实现契约补充

本节只补充当前实现必须遵守的契约，不替换前文的设计和使用模式。

### 8.1 当前核心对象

当前协程运行时由以下对象共同组成：

```text
CoroutineFrameControl
    ├── ExecutionLease
    ├── ContinuationLink
    └── ResumeGate

Task<T>
    └── CoroutineFrameControl + continuation lease

AsyncGenerator<T>
    └── GeneratorState<T>
        ├── producer handle
        ├── producer frame control
        ├── GeneratorStatus / GeneratorPhase
        ├── pending consumer ContinuationLink
        └── external ResumeGate

HTTP operation
    └── OperationState + OperationStatus
```

`std::coroutine_handle<>` 只是非 owning 句柄；只有 `CoroutineFrameControl` 的保护状态和实际 owner/lease 共同成立时，句柄才可以被恢复或销毁。

当前实现的恢复路径分为两类：

```text
外部事件：event -> ResumeGate -> CoroutineFrameControl::resume(handle)
内部交接：ContinuationLink + ExecutionLease -> 返回目标 handle
                                           -> symmetric transfer
```

内部 symmetric transfer 不得再对同一个目标调用一次显式 `resume()`。

### 8.2 FrameControl 与 ExecutionLease

`CoroutineFrameControl` 是独立于 coroutine frame 的生命周期仲裁者，维护：

```text
activeResumes = 当前受控 resume 调用链数量
activeLeases  = 当前 ExecutionLease 数量
activeCount   = activeResumes + activeLeases
```

`tryAcquire(handle)` 的契约：

1. 在 control mutex 内检查 `destroyed_`、句柄有效性；
2. 检查通过后增加 `activeLeases`；
3. 返回 move-only `ExecutionLease`；
4. 失败时不得继续交付该句柄。

`ExecutionLease` 只表示“frame 暂不能物理销毁”，不表示执行权、线程所有权、锁或 executor affinity。lease 的正常交接必须 move-transfer，禁止先 release 再重新 acquire。

`resume(handle)` 的契约：

1. 在 control mutex 内拒绝已销毁、无效或 `done()` 的句柄；
2. 增加 `activeResumes` 后执行 `handle.resume()`；
3. 返回前减少 `activeResumes`；
4. 最后一个 active protection 释放时处理 `deferredHandle`；
5. 同一 frame 不得并行执行两个 `resume()`。

协程体尚未声明挂起时，同一线程再次恢复同一帧会被拒绝：`resume()` 返回 `false`，计入 `executionConflicts()` 与 `ResumeMetrics::conflicts`（8.16），不排队补发。这个窗口只对绕过 `ResumeGate` 的直接恢复存在：外部事件总是先让 gate 进入 `Signaled`（8.4），`arm()` 之后才能恢复，而 `arm()` 是挂起前的最后一步；另一线程的恢复会在 control mutex 上等到协程体挂起，然后正常执行。所以计数非零表示调用方违反了契约，而不是丢失了合法的唤醒（`ResumeMetricsTest.ResumeOfAFrameThatIsStillRunningIsRefusedAndCounted`）。

`isDone(handle)` 的契约：

- 跟踪终态的帧（`Task`）只读原子的 `finished_` 与 `destroyed_`，不取 control mutex。`resume()` 在整个协程体运行期间持有该 mutex，所以任意线程都可以在协程体运行时调用 `isDone()`，它不会被协程体阻塞。`finished_` 在 `finish()` 内以 release 存储，读到 `true` 的线程能看到协程体写入的结果。
- 不跟踪终态的帧（生成器）读 `handle.done()`，仍在 mutex 内，与 `resume()` 互斥。
- 读到 `true` 之后销毁帧仍然安全：`finished_` 在 `finish()` 内置位，那是 final awaiter 对 promise 的最后一次访问。`finish()` 之前帧处于收尾阶段（已 `markFinalizing()`），此时的 `destroy()` 只登记延迟销毁，由 `finish()` 在置位之后执行。

`destroy(handle)` 是销毁请求，不是等待接口：

```text
destroyed_ == false && activeCount == 0
    -> destroyed_ = true
    -> 返回 true，调用方执行 handle.destroy()

destroyed_ == false && activeCount != 0
    -> destroyed_ = true
    -> 保存 deferredHandle
    -> 返回 false，保护归零后销毁
```

调用方只有在首次 `destroy()` 返回 `true` 时才可以直接调用 `handle.destroy()`。重复 destroy 不得再次销毁。

帧锁与协程体互斥：`resume()` 在整个协程体运行期间持有 control mutex。`tryAcquire()`、`destroy()`，以及不跟踪终态的帧（生成器）的 `isDone()` 取同一把锁，所以在另一线程上对一个正在运行协程体的帧调用它们，会等到协程体挂起或结束；只有 `Task::isDone()` 不取锁。因此协程体不能等待销毁者持有的资源，销毁者也不应在持有协程体所需的锁时销毁 `Task`。

### 8.3 ContinuationLink 两阶段契约

`ContinuationLink` 携带：

```text
handle + control + lease + targetLeaseSlot + sourceLeaseSlot
```

它有两个阶段：

```text
Empty
  │ registerTarget()
  ▼
Registered
  │ handle + control + lease slots，无 target lease
  │
  └─ acquireTarget()
       ▼
Transferred
       handle + control + target lease
```

- `registerTarget()` 用于所有等待中的续体：`co_await` 子 `Task` 的父协程，以及生成器的 pending consumer。只注册目标，等待期间不持有目标帧的租约。
- `acquireTarget()` 只能在真实 final/yield/close handoff 前调用：子 `Task` 在 final suspend 才取父协程的租约，生成器在 yield、final 或 close 时才取消费者的租约。
- `create()`（注册后立即 acquire）已不用于等待中的续体，保留给不跨越等待的短暂交接。
- link reset 或析构释放自身 lease；lease 已转移后由目标 promise 的 lease slot 管理。
- acquire 失败时必须清除 link，不得交付目标句柄：目标帧已销毁时，子 `Task` 结束后不恢复它。

等待期间不持有租约，是因为租约会推迟帧的物理销毁：父协程被销毁时，如果挂起的子协程持有它的租约，销毁请求只能登记为延迟，而子协程要等一个永远不来的事件才会 final，整条链就滞留到令牌被取消为止（嵌套深度越大越明显，令牌回调也随之滞留）。改为 final 时才取租约后，销毁父协程立即销毁它的帧，其 `TaskAwaiter` 随之销毁并销毁子协程帧，叶子等待逐层展开。回归用例：`ChainDestroyTest.DestroyingTheRootOfANestedChainReleasesEveryFrame`、`ChainDestroyTest.ADestroyedParentNeverRunsAfterItsChildFinishes`、`ChainDestroyTest.ClosingAGeneratorThatAwaitsAParkedChildReleasesTheChain`、`ChainDestroyTest.DestroyRacingChildCompletionReleasesEveryFrame`。

### 8.4 ResumeGate 契约

一个外部等待只能有一个负责恢复的 `ResumeGate`。包装 awaiter 和底层 awaiter 必须共享同一个 gate，不得各自创建恢复入口。

```text
Pending --arm()--> Armed --signal()--> Resuming
    │                  │                  │
    └--signal()--> Signaled              └--> frameControl.resume()

Pending/Armed --disarm()--> Disarmed
```

状态语义：

- `Pending`：gate 已创建，外部事件尚未完成注册；
- `Armed`：外部事件可以竞争恢复；
- `Signaled`：事件先于 arm 到达，后续 arm 返回 `false`，协程不挂起；
- `Resuming`：唯一胜出者已取出 handle/control，正在执行受控 resume；
- `Disarmed`：关闭状态，后续 signal no-op。

`signal()` 必须满足 at-most-once：

- `Pending -> Signaled` 只记录提前通知，不直接恢复；
- `Armed -> Resuming` 只有一个胜出者；
- `Signaled`、`Resuming`、`Disarmed` 不得重复恢复。

`ResumeGate` 只仲裁恢复竞争，不能单独证明 frame 存活；恢复前仍必须经过 `CoroutineFrameControl` 的 destroyed/done 检查。

### 8.5 GeneratorState 与 AsyncGenerator

`GeneratorState<T>` 是 generator producer ownership 和状态协调的唯一共享状态，持有：

```text
producer handle
producer frameControl
GeneratorStatus
GeneratorPhase
failure exception_ptr
pending consumer Registered ContinuationLink
external ResumeGate
consumer generation
```

`GeneratorStatus` 只有以下转换：

```text
Running -> Completed
Running -> Failed
Running -> Closed
```

终态不可逆。`Completed` 或 `Failed` 上调用 `close()` 仍执行 teardown，但保留原终态；重复 close、completion、failure 不得重复恢复或销毁。

`GeneratorPhase` 的语义为：

| phase | 语义 |
|---|---|
| `InitialSuspended` | producer 已创建，尚未驱动 |
| `Executing` | producer 正在执行 |
| `WaitingConsumer` | 刚登记 consumer，随即被 `markExecuting()` 改为 `Executing`，不会作为静止状态出现 |
| `WaitingExternal` | producer 等待 timer、fd、completion 或 IO 事件 |
| `Yielded` | producer 停在 `co_yield` 上：value 已产生，保持到下一次 `next()` 把它改为 `Executing` |
| `FinalSuspended` | producer 已完成、失败或关闭 |

phase 是 owner executor 上的状态，不是并发锁。

### 8.6 `next()` 与 PendingConsumerRegistration

`next()` 的 awaiter 在 pending consumer 阶段执行以下顺序：

1. 在触碰任何租约之前检查 producer 是否停在可恢复的位置：generator 仍为 `Running` 且 phase 不是 `InitialSuspended` 或 `Yielded` 时抛出 `std::logic_error`；
2. 释放 consumer 旧 transfer leases；
3. 检查 generator status、producer handle 和已有 value；
4. 通过 `registerTarget()` 注册 consumer continuation；
5. generation 自增；
6. 安装 typed `PendingConsumerRegistration<T>`；
7. producer 标记 `Executing`；
8. 返回 producer handle，执行 symmetric transfer。

单消费者契约：同一个 generator 同一时刻只允许一个未决的 `next()`。producer 只有停在初始挂起点或 `co_yield` 上（phase 为 `InitialSuspended` 或 `Yielded`）才能被 `next()` 的 symmetric transfer 恢复。上一个 `next()` 还没返回时（phase 为 `Executing` 或 `WaitingExternal`），producer 要么正在运行，要么停在自己的外部等待上，这时再恢复它会越过等待，而事件并没有发生。所以这种 `next()` 在第 1 步抛出 `std::logic_error`，不改变 generator 的任何状态：已登记的 consumer 仍在等待，producer 仍停在原处，事件到来后正常交出 value。consumer 销毁后 producer 仍在服务它的请求，期间的 `next()` 同样被拒绝；请求完成后产生的 value 保留给下一次 `next()`。generator 不提供多个消费者排队的语义。

`PendingConsumerRegistration<T>` 持有：

```text
weak_ptr<GeneratorState<T>>
consumer handle
generation
```

它不持有 consumer frame lease。registration 析构或 reset 时，只撤销 state 中与 handle 和 generation 同时匹配的 pending link。这样 consumer frame 可以在 pending `next()` 期间销毁，awaiter 能通过 RAII 完成撤销，不需要向通用 frame control 注入 generator-specific callback。

`await_resume()` 必须先 reset 当前 registration，然后：

- `Failed`：重新抛出 producer 保存的异常；
- producer 不存在：返回 `nullopt`；
- 有 value：移动 value、清空 producer value 后返回；
- 无 value：返回 `nullopt`，表示 EOS 或关闭。

### 8.7 yield/final handoff

producer 的 `co_yield` 和 `final_suspend` 交接必须遵守：

1. 释放 producer 旧 transfer leases；
2. 摘取 Registered consumer link；
3. acquire consumer target lease；
4. acquire producer transfer lease；
5. 将 target lease move 到 consumer promise slot；
6. 将 producer transfer lease move 到 consumer promise slot；
7. 清除 state pending consumer；
8. 返回 consumer handle，执行 symmetric transfer。

任一 acquire 失败都必须清除 link，不得返回悬空句柄。pending consumer 在等待期间不应持有 lease，避免 owner destroy 被 pending lease 阻塞。

### 8.8 `AsyncGenerator::close()` 生命周期契约

`AsyncGenerator::close()` 是 owner executor/thread 上的同步逻辑关闭和 frame destroy request。它不是 graceful producer close，也不等待底层 IO、timer、线程池任务或 callback 完成；但可能因为 control mutex 短暂串行化，或同步恢复 pending consumer。

close 的顺序固定为：

```text
1. 已 Closed：直接返回
2. disarm 当前 external ResumeGate
3. 对 producer 调用 frameControl.destroy(producer)
4. Running -> Closed；Completed/Failed 保持原终态
5. 再次 disarm external gate，覆盖 gate 安装竞态
6. phase -> FinalSuspended
7. 摘取 pending consumer Registered link
8. producer_ = nullptr
9. destroyNow == true 时调用 producer.destroy()
10. handoff 前 acquire consumer target lease
11. 成功则只恢复 consumer 一次，使 next() 收到 EOS
```

如果 external signal 已进入 `Resuming`，close 不抢夺这次受控 resume；destroy request 通过 active resume 保护转为 deferred destroy。若 close 先完成 gate disarm，之后 signal 必须 no-op。

close 返回后，state 不得继续生产，producer handle 不得再被调用，external signal 不得重新启动 producer，pending consumer 不得被恢复两次，frame 不得被销毁两次。

### 8.9 外部 await、取消与 IO

timer、completion、fd 和 IO awaiter 的通用顺序是：

```text
releaseTransferLeases(promise)
    -> markExternalAwait(promise)
    -> 创建唯一 ResumeGate
    -> 注册外部事件、取消和超时
    -> arm gate
    -> signal 竞争恢复
    -> await_resume 读取结果并释放 registration/cleanup
```

注册完成前事件到达时，gate 必须进入 `Signaled`，arm 返回 `false`，协程不得挂起。

`AsyncIOOperation` 的 `Completed`、`Failed`、`TimedOut`、`Cancelled` 由唯一终态竞争者提交。胜出者负责发布结果、保存错误、撤销 timer、提取 cleanup 和触发 gate；cleanup 在锁外执行。

`FdReadyAwaiter` 的隐式令牌回调与它的 fd 续体、截止时间定时器共享同一个 gate，不能形成两个独立 resume 入口。取消 callback 只能发出取消或 signal 请求，不能保存并直接访问已销毁 frame。

取消统一为异常：挂起点在取消时抛出 `CancelledException`，包括执行器拒绝派发的恢复（等待结束，续体不运行）。覆盖的挂起点有 `TimerAwaiter`、`YieldAwaiter`、`CompletionAwaiter`、`Job::join`、`AsyncIOOperationBase` 与 `FdReadyAwaiter`。fd 注销或等待者被替换是终态通知而非取消：`FdReadyAwaiter` 报告未就绪，`AsyncIOOperationBase` 以 `Error::SocketClosed` 返回。`AsyncRead` 与 `AsyncWrite` 在就绪唤醒后重试一次系统调用，仍无数据时报告 `WouldBlock`，不再把唤醒当作成功返回。fd 等待的取消在 `waitFdReadyWithTimeout` 转换为“未就绪”，调用方用 `coro::cancellation_requested()` 区分取消与超时；DNS 取消同样抛出，`ConnectionFactory` 在边界映射为 `Error::Cancelled`。

同一 fd 的同一方向（读或写）同时只允许一个等待者，读与写互相独立。后注册的等待者替换先注册的：`EventLoop` 记录一条警告，被替换者以终态通知结算（`FdReadyAwaiter` 报告未就绪，`AsyncIOOperationBase` 返回 `Error::SocketClosed`），不会停放在原处。需要多个消费者时，由调用方在 fd 之上自行串行化等待（`EventLoopRaceTest.SecondWaiterOnTheSameFdAndDirectionEndsTheFirstAsNotReady`、`EventLoopRaceTest.ReadAndWriteWaitersOnTheSameFdAreIndependent`）。

### 8.10 OperationState 与 OperationStatus

HTTP operation 层在协程状态机之上维护：

```text
enum class OperationStatus {
    Running,
    Completed,
    Cancelled,
    Closed,
    Failed,
};
```

合法转换为：

```text
Running -> Completed
Running -> Cancelled
Running -> Closed
Running -> Failed
```

终态不可逆；重复 complete、fail、cancel、close 都是 no-op。

- `OperationState::cancel()`：同步提交 `Running -> Cancelled`，并取消内部 token；不等待底层任务退出。
- `OperationState::close()`：同步提交 `Running -> Closed`，并取消内部 token；不表示底层 IO 已完成。
- operation 析构必须执行 close。
- streaming 收到 EOS 时，如果 operation token 已取消，状态优先为 `Cancelled`；否则为 `Completed`。
- `HttpRequest::cancel()` 必须调用每个 running operation 的 `OperationState::cancel()`，不能只调用 token.cancel()。

### 8.11 执行域与禁止事项

producer、consumer、`next()`、`co_yield`、completion、close，以及 generator state 的普通字段访问必须在同一 owner executor/thread 串行执行。跨线程只能投递到 owner executor。

唯一允许和 owner close 竞争的跨线程入口是已注册 external `ResumeGate::signal()`；该竞争由 gate 状态和 frame control 的 active/destroyed/deferred 状态共同收敛。

执行器契约：`tryExecute` 返回 `true` 的任务一定会在该执行器上执行且只执行一次；返回 `false` 表示未被接受，调用方必须把对应续体转交回退执行器（见 8.12）。`SerialExecutor`、`ThreadPool`、`ThreadScheduler` 与线程退出路径都遵守此契约：停止后拒绝新任务，已接受的任务在线程退出前全部执行。`ThreadScheduler::stop()` 与 `SerialExecutor` 的析构会等待它们完成后才返回，因此不要在持有这些任务所需的锁时停止。停止后通过 `tryExecute` 投递的任务（包括已接受任务内部投递的）返回 `false`。定时器的语义是停止即取消：未到期的定时器被取消，停止之后不再启动任何定时回调。`StreamSource` 等以 `closed_` 标志关闭的组件，其排队任务在关闭后只做空操作。

以下行为违反契约：

1. 绕过 `CoroutineFrameControl` 直接恢复受控 frame；
2. 为同一次外部等待创建两个独立 gate；
3. 在 pending consumer 阶段用 lease 阻塞 owner frame 销毁；
4. close 后继续使用 producer handle 或 promise；
5. 跨线程直接调用同一 generator 的 `next()`、close 或 producer 状态操作；
6. 用 `void*`、裸 awaiter `this` 或 generator-specific callback 注入通用 frame control；
7. continuation handoff 先 release 再重新 acquire，制造销毁空窗；
8. 把 `OperationStatus::Closed` 当作底层 HTTP/IO 已完成；
9. 执行器拒绝派发后在调用方线程内联恢复续体，被拒绝的续体只能在回退执行器上恢复（见 8.12）。
10. 在协程首次 `resume()`（生成器为首次 `next()`）之后绑定令牌或执行域。挂起的 awaiter 已登记自己的回调，之后的绑定会静默丢失或改投已登记的等待者，因此抛出 `std::logic_error`（见 8.13）。
11. 在上一个 `next()` 尚未返回时再次 `next()`，包括上一个 consumer 已销毁、producer 仍在服务其请求的情况。producer 停在自己的外部等待上，恢复它会越过等待，因此这次调用抛出 `std::logic_error`（见 8.6）。

### 8.12 组合与让出：when_all、when_any、AsyncScope、yield

执行域：子任务没有域时，`when_all` 与 `AsyncScope::spawn` 先在调用方线程上启动到第一个挂起点，之后由事件源线程恢复。子任务有非 inline 的域时，帧绑定该域：首次启动投递到它，挂起后的每次恢复也经它派发，子任务内 `co_await` 的孙任务按 8.13 继承。域的来源：`when_all(exec, ...)` 的 `exec`，未显式传入时是被等待协程自己的 executor；`AsyncScope(executor)` 或 `setDefaultExecutor`；`when_any` 没有 `exec` 重载，子任务的域由传入的 `AsyncScope` 决定。子任务自己 `setExecutor` 绑定的域优先，挂起后的恢复在它自己的域上。`Job` 的 `options.executor` 同样绑定到根协程（8.18）。有域的子任务被取消唤醒时，恢复同样经该域派发，不在取消线程上同步展开，所以 `cancelAll()` 返回时子任务可能尚未退出。

执行器归属有两种形式。拥有型（`shared_ptr`，`when_all(std::shared_ptr<IExecutor>, ...)`、`setDefaultExecutor(std::shared_ptr<IExecutor>)`、`AsyncScope(std::shared_ptr<IExecutor>)`）：拥有者（awaiter 或 scope）持有执行器直到子任务全部结束，调用方可以随时放开自己的引用。弱引用（`weak_ptr<E>`，同样三处）：不延长执行器寿命。弱引用的派发先加锁，成功时把强引用捕获进排队的任务，所以已接受的派发保持执行器存活到任务运行完毕；挂起中的子任务不保持它。实现为 `detail::WeakExecutor` 转发器，派发链路与拥有型相同，门与恢复机制不变。执行器已销毁（或 `weak_ptr` 为空）后的派发一律被拒绝，不回落到等待者的执行域：未启动的子任务不启动（`spawn` 返回 `false`，`when_all` 的子任务以 `CancelledException` 结束），挂起的子任务在恢复时以 `CancelledException` 在回退执行器上展开。引用别名（`IExecutor&`）已删除：它没有寿命保证，调用方须自行保证执行器活到所有子任务展开，而 scope 析构允许不 join。空指针表示不指定域，与 `AsyncScope(nullptr)` 一致。回归用例见 7.2「执行器归属」。

并发语义以 Kotlin 的结构化并发为准（`JobSupport.kt` 与 `Executors.kt` 已核实）。执行器拒绝派发时，等待被取消（`CancelledException`），协程仍在回退执行器 `CoroutineScheduler::fallbackExecutor()` 上恢复，使帧能展开；这与 Kotlin 的 `cancelJobOnRejection` 加 `Dispatchers.IO` 回退一致。回退执行器 `CoroutineScheduler::fallbackExecutor()` 是进程级、永不析构的线程池，首次使用时启动，因此析构期间的派发仍能落到存活的池上。它永不关闭，回退派发不会被拒绝；若仍被拒绝，说明不变量被破坏，记录错误日志并 `std::terminate()`，绝不在调用方线程内联恢复。Kotlin 没有这一分支（`Dispatchers.IO` 不拒绝派发）。代价：进程退出时仍在回退队列中的协程不再执行，其局部对象的析构也不会运行，帧随进程回收。

`when_all(Task<Ts>...)`：

- 所有子任务结束后才恢复，结果是各子任务返回值组成的 `std::tuple`；只要有一个子任务返回 `void`，`co_await` 的结果就是 `void`。
- 子任务抛出非取消异常时，取消兄弟子任务（Kotlin：失败的子 Job 取消父与兄弟），等全部结束后重新抛出第一个非取消异常；取消异常不会掩盖根因。
- 等待者（父协程）被销毁时，取消全部子任务（Kotlin：取消父会取消子）。子任务以 `CancelledException` 退出，帧在最后一个租约释放后销毁（8.2）。
- 子任务的派发被执行器拒绝时，该子任务以取消结束且不启动，不会取消兄弟（Kotlin：被拒绝的任务是取消，不是失败）。
- 无参数时不挂起。

`when_any(AsyncScope&, Task<Ts>...)`，至少一个子任务：

- 第一个结束的子任务获胜，结果是 `WhenAnyResult{winnerIndex, winnerValue}`，`winnerValue` 是 variant，`void` 子任务对应 `VoidResult`。
- 获胜者抛出的异常会重新抛出；落败者的结果被丢弃。落败者（以及获胜者）的非取消异常同样使 scope 失败：取消 scope，兄弟子任务随之取消，异常在 `join()` 时重新抛出（Kotlin：非 supervisor 的子 Job 失败取消父，与是否被 await 无关，见 `JobSupport.kt` 的 `cancelParent`）。
- 子任务由传入的 `AsyncScope` 托管。落败者不会因为胜出而被取消，继续运行直到结束，或 scope 被取消（`cancelAll()`、析构、其它子任务的非取消失败）。
- 全部子任务都被执行器拒绝时，等待者以 `CancelledException` 结束（Kotlin：全部分支被取消的 select 自身被取消）。
- scope 必须在等待期间存活。

`AsyncScope`：

- 执行域可在构造时传入：`AsyncScope(std::shared_ptr<IExecutor>)` 或 `AsyncScope(std::weak_ptr<E>)`，`setDefaultExecutor` 同样提供两种形式（见上文执行器归属）。子任务的首次启动与挂起后的每次恢复都在该域上。拥有型的子任务持有该域直到结束，因此分离的子任务在 scope 析构后仍能在它上面恢复；弱引用型不保持执行器存活。在首次 `spawn` 之后调用抛出 `std::logic_error`，已启动的子任务不会静默改投到新域。
- `spawn()` 可从任意线程调用，包括与 `cancelAll()` 并发：子任务 id 原子分配，children 表受锁保护。`spawn(task, token)` 的子任务同时受 scope 令牌与 `token` 控制（两者合并，任一取消即取消），`cancelAll()` 必达；`spawn(task)` 的子任务持有 scope 令牌的子令牌，取消它只影响它自己。返回 `false` 表示执行器拒绝了子任务，子任务从未启动（Kotlin：被拒绝的 `launch` 不执行 body）；子任务可以在任意线程完成。空 `Task`，以及已启动或已完成的 `Task`，调用 `spawn` 时在登记前抛出 `std::logic_error`，子任务表与 scope 状态不变（这是调用方错误，不是 scope 失败）。
- 子任务表位于共享核心 `ScopeCore` 中。子任务持有核心的强引用（Kotlin：子协程保持父 Job 存活），结束时在自己的执行线程上把自己从表中摘除（Kotlin：`parentHandle.dispose()`），表条目数等于未结束的子任务数。
- 摘除时帧仍在执行，帧的销毁推迟到本次 resume 返回（8.2）。
- 子任务抛出非取消异常时，scope 被取消（Kotlin：失败的子 Job 取消父），保留第一个非取消异常，供 `join()` 重新抛出。
- `join()` 是挂起式等待（Kotlin：`Job.join()`，不阻塞线程）：所有子任务结束后恢复，并重新抛出保留的异常。两种取消互不相同：等待者自身被取消时，`join()` 抛 `CancelledException`，不取消子任务（Kotlin：取消 join 不取消子 Job）；scope 被 `cancelAll()` 或析构取消时，子任务随之取消，`join()` 在子任务全部结束后正常返回，不抛取消。`join()` 本身不取消子任务，需要停止时先调用 `cancelAll()`。
- 不响应取消的子任务会让 `join()` 一直等待，与 Kotlin 一致。
- 析构只取消 token，不等待子任务。仍在运行的子任务与核心一起保持存活，直到结束后回收；析构时仍有子任务会记录一条警告日志。协程不会在 scope 析构后访问已释放的资源。库内的叶子挂起点都经 `attach(…, LeafEdge{})` 登记令牌，组合等待（`when_all`、`when_any`）则经子任务的令牌被取消，所以取消一定会唤醒库内的挂起点，子任务一定会结束；只有未绑定令牌的自定义 awaiter 且事件源永不触发时，子任务与核心会一直保留（见 8.20）。
- 析构不能挂起，因此拥有者应在销毁前 `co_await scope.join()`。在 scope 自己的执行器线程上销毁 scope 不会死锁。
- 已移除 `waitAll()` 和带超时的析构等待。
- 子任务的返回值被丢弃；非取消异常的处理见上文。析构时若失败从未被 `join()` 观察到，记录一条错误日志，不会静默丢失。
- `cancelAll()` 置位 `token()` 并唤醒阻塞在库内 awaiter 上的子任务，子任务以 `CancelledException` 退出（见 8.13）。析构同样取消。仍在纯计算中的子任务只能自己检查 `token()`。

`co_await slark::yield()`：

- 总是先挂起，向 `CoroutineScheduler` 投递 0 ms 回调，之后在调度器线程上继续。回调先于挂起完成到达时（见 8.9），协程不挂起，在原线程继续。
- 挂起时绑定取消：令牌已取消或挂起期间被取消时，恢复后抛出 `CancelledException`（见 8.13）。
- 协程在挂起期间被销毁是安全的：`YieldAwaiter` 析构时 `disarm` gate，迟到的回调不会再恢复已销毁的 frame。

### 8.13 隐式取消传播

取消源是一个 `CancellationToken`，绑定在根 `Task` 上：

```text
Task<T>::attach(SpawnEdge{}, scope, extra)
    -> promise.cancelToken_ (std::optional，未绑定时不分配)
```

传播规则：令牌由"边"决定，不由组合子决定。组合子只声明自己创建的作用域，不读取父令牌，也不手写合并。

1. **await 边**：`co_await childTask` 与生成器 `next()` 经 `attach(child, AwaitEdge{}, awaitingToken)` 调用 `PromiseCancellation::inheritCancelToken`。子帧没有令牌则继承父协程的；两边都有则以 `CancellationToken::link(child, parent)` 合并，任一取消即取消。生成器每次 `next()` 都会重新进入，但令牌只在消费者变化时重建：帧记住自己的令牌（首次继承前的 `ownToken_`）与当前被继承的令牌（`inheritedFrom_`，同一性用 `identity()` 判断）。同一消费者的连续 `next()` 不做任何事；换了消费者时，从自身令牌与新消费者的令牌重新 link，旧消费者的 link 随之释放，因此取消的始终是当前消费者；交给无令牌的消费者时，令牌回到自身令牌。令牌链深度与消费者令牌上的回调数只随交接次数变化，不随 `next()` 次数增长。
2. **spawn 边**：组合子与 `AsyncScope` 创建新的子协程时，经 `attach(child, SpawnEdge{}, scope*, extra*)`：令牌是 `scope` 的子令牌（`scope` 为空则独立），`extra` 存在时与之合并（`CancellationToken::link`）。父取消则它取消，它取消不影响父。子帧必须在启动前挂接，且只能挂接一次。
   - `when_all` 与 `when_any` 派生 `CompositeWaitBase`（`CompositeWaitBase.hpp`）。基类在唯一的 `await_suspend` 里读一次等待者的令牌，交给派生类的 `start(gate, inherited, awaitingDomain)`。`when_all` 的组令牌是独立的 `CancellationToken{}`，每个子任务 `attach(子, SpawnEdge{}, &组, inherited)`；非取消失败与等待者销毁时取消组令牌。`when_any` 把 `inherited` 作为额外令牌交给 `AsyncScope::spawn`。组令牌与父令牌都不唤醒等待者自己，等待者只由子任务结束唤醒。
   - `AsyncScope::spawn(task)` 与 `spawn(task, token)` 都经 `startChild`，由 `attach(包装, SpawnEdge{}, &scope, extra)` 挂接，`spawn(task)` 的 `extra` 为空。
   - `Job` 的根协程经 `attach(body, SpawnEdge{}, &options.token)` 挂接；`Job::cancel()` 与析构取消它。`with_timeout` 的内部协程经 `attach(子, SpawnEdge{}, &超时令牌)` 挂接，等待子任务时 await 边再与调用方令牌合并。
3. **叶子边**：挂起点把取消回调注册到当前协程的令牌上，唯一入口是 `attach(frame, LeafEdge{}, cb)`（内部 `registerCallbackOrRun`）。`ExternalWaitBase` 与 `AsyncIOOperationBase` 的挂起都先经 `enterExternalWait` 建门，再 `attach(LeafEdge)`。两者的顺序不同：`ExternalWaitBase` 先启动事件源，IO 必须先登记门再启动操作，并自行仲裁四个终态，所以 `arm` 与事件源的先后保留在各自的基类里。`AsyncIOOperationBase` 不再有显式的 `setCancellationToken`：令牌只来自当前协程。

守卫：`when_all`、`AsyncScope`、`Job` 直接启动的包装协程（`whenAllChild`、`scopeWrapper`、`runJobBody`）以 `co_await RequireAttachedEdge{}` 作为第一条语句检查 `isAttached()`。未经 `attach` 就启动（例如改用手写的 `setCancellationToken`）时记录错误日志，同时写 stderr（使致命信息不依赖日志通道，死亡测试据此精确匹配），然后 `std::terminate()`。根绑定经 `attach(根, SpawnEdge{}, …)` 挂接（见 8.21）；`Task::setCancellationToken` 与 `AsyncGenerator::setCancellationToken` 只供测试夹具使用。包装协程若用它们代替 `attach`，同样不满足守卫（`TokenEdgeGuardDeathTest.HandWrittenBindingDoesNotSatisfyTheGuard`）。守卫只能发现漏挂接的启动路径；选错边（该用 AwaitEdge 却用了 SpawnEdge）由契约套件负责。

读取一个 promise 的令牌与执行域只有 `cancellationTokenOf(promise)` 和 `executorOf(promise)` 两个入口，对没有对应成员的 promise 返回空，所以组合子在 `Task` 与 `AsyncGenerator` 的协程体内用法相同（`StructuredConcurrencyTest.WhenAllRunsInsideAGeneratorBody`、`WhenAnyRunsInsideAGeneratorBody`）。

新增组合子时按形状选择，不要手写令牌的继承、合并或登记：

| 形状 | 做法 |
|---|---|
| 只 `co_await` 子任务的协程形（`then`、`and_then`、`or_else`） | 什么都不做，await 边自动继承 |
| 另起子协程的协程形（`with_timeout`） | 子协程 `attach(子, SpawnEdge{}, &来源)`，await 边再与调用方令牌合并 |
| 等待者形（并发等多个子任务） | 派生 `CompositeWaitBase`，用基类交付的 `inherited` 作为额外来源，组令牌为 `CancellationToken{}`，子任务 `attach(子, SpawnEdge{}, &组, inherited)` |
| 根形（脱离调用帧运行的任务，如 `Job`；HTTP 入口返回的 `Task` 与生成器） | `attach(根, SpawnEdge{}, &显式来源[, &第二来源])`，或 `根.attach(SpawnEdge{}, …)`；`Job` 保存返回值作为自己的令牌 |
| 叶子（等外部事件） | 派生 `ExternalWaitBase`，或挂起中调用 `enterExternalWait` 后 `attach(frame, LeafEdge{}, cb)` |
| 由组合子直接启动的包装协程 | 首条语句 `co_await RequireAttachedEdge{}` |
| 叶子（等外部事件） | 派生 `ExternalWaitBase`，不直接接触令牌 |

每个新入口与每种新叶子都要加进 `test/base/TokenContractTest.cpp` 的类型列表。套件对每个入口与叶子的组合检查五条契约：取消上游令牌必须到达叶子；上游已取消时不得进入叶子等待；取消组合子自己的作用域不得取消上游；重复使用时上游令牌上的回调数不增长；入口销毁后回调数回到基线。突变验证（撤销 await 边、撤销叶子登记、撤销 `when_all` 的挂接、撤销 Job 的守卫）各自只让对应用例变红。

绑定时机：`Task::setCancellationToken` 与 `Task::setExecutor` 只能在首次 `resume()` 之前调用。协程启动后绑定会静默丢失，因为挂起的 awaiter 已经登记了它自己的回调，新令牌无法唤醒它，因此启动后调用抛出 `std::logic_error`，`nullptr` 执行器同样适用。`AsyncGenerator::setExecutor` 与 `setCancellationToken` 同样以首次 `next()` 为界；生成器已关闭时没有可绑定的生产者，调用为空操作。移动 `Task` 不改变启动状态，移动后的对象仍拒绝绑定。

`co_await` 一个已经启动的 `Task` 同样被拒绝：它正挂在自己的等待上，若直接转入会在没有事件的情况下恢复该等待。`TaskAwaiter::await_suspend` 在触碰任何租约之前检查启动状态，抛出 `std::logic_error`，子协程保持挂起、不被恢复。已完成的子协程不受影响，直接返回结果。

取消的语义：

- 取消与正常完成竞争：`CompletionAwaiter` 与叶子 awaiter 通过 `ResumeGate::signalCancel()` 仲裁，只有一方恢复协程；`AsyncIOOperationBase` 通过 `SharedCompletion` 的终态仲裁，只有第一个终态生效。已经完成的结果不会因为之后的取消而丢失。
- 令牌在挂起前已取消时，awaiter 不挂起。`TimerAwaiter`、`CompletionAwaiter`、`YieldAwaiter`、`AsyncIOOperationBase` 与 `FdReadyAwaiter` 恢复后抛出 `CancelledException`；`AsyncIOOperationBase` 不启动 IO。
- 取消回调注册在 awaiter 成员 `CancellationTokenRegistration` 上，awaiter 析构时自动注销，不会访问已销毁的协程。
- 注册必须用 `CancellationToken::registerCallbackOrRun`：它在锁内原子地检查并注册，已取消时在锁外立即运行回调。先 `isCancelled()` 再 `registerCallback()` 是两步操作，中间的取消会丢失，协程挂起后永远不被唤醒。`AsyncIOOperationBase` 的登记与 `DnsResolver::resolveAsync` 曾因此出现丢失取消，现已修复（`AsyncIOOperationTest.CancelRacingTheSuspensionNeverLosesTheWake`）。`AsyncIOOperationBase` 曾另有一个显式的 `setCancellationToken`，生产代码没有调用方，已删除。
- `CancellationToken::link(a, b)` 的结果持有 `a` 与 `b` 的状态：转发注册只持弱引用，若输出不保有输入，临时的中间令牌在调用返回后被释放，取消链即断开（`CancellationPropagationTest.LinkedTokenKeepsItsSourcesAlive`）。
- 没有绑定令牌的协程不注册任何回调，行为与之前一致。
- 没有绑定执行器的协程在调用 `cancel()` 的线程上同步恢复，可能嵌套在调用者的栈帧内。调用方不得在持有会被协程完成路径获取的锁时调用 `cancel()`。

`co_await coro::cancellation_token()` 返回当前协程绑定的令牌（`std::optional<CancellationToken>`），供需要协作式检查的代码使用。

### 8.14 with_timeout

```cpp
auto result = co_await coro::with_timeout(fetchOnce(url), 5s);
```

子任务完成、计时器到期、父令牌取消三者竞争同一个决议，先到者决定结果：

- 子任务先完成：返回其结果，或重新抛出其异常。
- 计时器先到：取消子任务，并抛出 `TimeoutException`。无论子任务之后以什么方式结束（返回值、异常、或吞掉取消后正常返回），结果都是超时。
- 父协程的令牌先取消：抛出 `CancelledException`，不转换为超时。子任务吞掉取消后正常返回，也仍以取消为准。

超时和取消后，`with_timeout` 仍会等子任务结束才恢复，子任务不会在后台运行。定时器在返回前撤销，不留下待触发的回调。

调用方在传入前绑在子任务上的令牌（`task.setCancellationToken(t)`）不会被超时覆盖：超时令牌经 `attach(…, SpawnEdge{})` 挂接在内部包装协程上；定时回调只弱引用共享状态，状态持有调用方令牌，因此已完成的 `with_timeout` 不会让调用方令牌在超时到期前保持注册（`TokenContractTest/WithTimeout_*`），包装协程 `co_await` 子任务时，子任务自己的令牌经 await 边与之合并，任一取消都会到达叶子等待（`CoTimeoutTest.TokenBoundToTheChildItselfStillCancelsIt`）。

限制：
- 超时只在取消点生效。隐式取消覆盖的挂起点是 `TimerAwaiter`、`CompletionAwaiter`、`YieldAwaiter`、`AsyncIOOperationBase` 与 `FdReadyAwaiter`（读写等待、TLS 握手）。取消后 `FdReadyAwaiter` 抛出 `CancelledException`；超时与取消在 IO 边界统一为未就绪，由调用方用 `coro::cancellation_requested()` 区分。
- 纯计算和不挂起的循环不会被打断，只能在下一个挂起点退出。

### 8.15 Task 组合子：then、and_then、or_else

```cpp
auto task = fetchOnce(url)
    .then([](HttpResponse r) { return r.statusCode; })
    .or_else([](std::exception_ptr) { return -1; });
```

- `then(f)`：`f(T)` 的返回值作为新结果；`Task<void>` 上的 `then(f)` 调用 `f()`。
- `and_then(f)`：`f(T)` 返回 `Task<U>`，结果展平为 `U`。
- `or_else(f)`：接收上游异常的 `exception_ptr`，返回值替代该异常，结果类型仍为 `T`。
- 上游抛异常时，`then` 与 `and_then` 不调用 `f`，异常原样传递。
- `or_else` 不拦截 `CancelledException`：取消必须继续向上传播。它会拦截 `TimeoutException`，因此 `with_timeout(...).or_else(fallback)` 可以把超时转换为回退值。

这些组合子都返回新的 `Task`，把上游的所有权移入其协程帧，链式调用后原对象处于无效状态。

- 空 `Task`（没有协程帧）传给 `then`、`and_then`、`with_timeout` 时，结果抛出 `std::runtime_error`；传给 `or_else` 时，异常交给回退函数处理。

### 8.16 恢复派发计数

`ResumeGate` 派发事件恢复（IO 就绪、定时器、完成通知）时计数，读取 `CoroutineScheduler::instance().resumeMetrics()`：

- `inlined`：没有执行域（或执行域为 inline），在信号线程上直接恢复。
- `dispatched`：交给执行域的恢复次数。在交付前计数，已运行的恢复一定能看到自己的计数。
- `refused`：执行域拒绝、转交回退执行器的次数，即 8.11 的拒绝路径。
- `pending`：已交给执行域、尚未开始运行的恢复数。交付时加一，开始运行时减一；被拒绝时撤销。
- `conflicts`：同一帧的协程体仍在运行时被再次恢复而拒绝的次数（8.2）。由 `CoroutineFrameControl::resume` 直接计数，不经 `ResumeGate`；非零表示有调用方绕过了 gate 协议。
- `queueLatencyUsTotal`、`queueLatencyUsMax`：从交付到开始执行的排队时间，单位微秒。

限制：计数是进程级的，用差值统计时若有无关协程同时恢复，差值只是下界。除 `conflicts` 外只统计 `ResumeGate` 派发的恢复；`AsyncScope::spawn` 与 `when_all` 启动子协程不经过这里。每次派发多一次时钟读取与原子计数，热路径开销尚未在真实负载下测量。

### 8.17 挂起点闭环总表

每个挂起点都必须有且只有一个终态：恢复、取消、或被显式丢弃。下表列出每类挂起点的唤醒源，以及停止、析构和取消时的行为。

| 挂起点 | 唤醒源 | 取消时 | 销毁或停止时 | 证明 |
|---|---|---|---|---|
| `TimerAwaiter` | 定时器回调 `signal()` | `signalCancel()`，抛 `CancelledException` | 撤销定时器，`disarm` | `CancellationPropagationTest.TimerWakesOnCancel` |
| `YieldAwaiter` | 零延迟定时器 | 同上 | `disarm`，迟到回调无效 | `YieldTest.DestroyingASuspendedCoroutineIsSafe`、`CancellationPropagationTest.YieldRacesCancelResolvesOnce` |
| `CompletionAwaiter::Waiter` | `complete()` / `fail()` | `signalCancel()` | 生产者销毁：仍挂起的等待者被 `disarm`，不再唤醒；帧随持有者释放 | `CompletionAwaiterTest.FrameOfAWaiterOrphanedByCompleterIsReleasedWithItsOwner`、`CompletionAwaiterTest.PendingCompletionCannotResumeDestroyedFrame` |
| `AsyncIOOperationBase` | 唯一终态竞争者（完成、失败、超时、取消） | `requestCancel()`，恢复时抛出 `CancelledException` | 析构 `cancel()` 只标记终态，结果不可观测 | `AsyncIOOperationTest.FailRacingCancelHasExactlyOneOutcome`、`AsyncIOOperationTest.CompleteRacingTimeoutHasExactlyOneOutcome`、`SuspensionMatrixTest.IoMidCancelEndsCancelledAndLaterCompletionIsInert`、`SuspensionMatrixTest.ReadWokenByReadinessRetriesAndReturnsTheData`、`SuspensionMatrixTest.ReadUnregisteredReportsSocketClosed` |
| `FdReadyAwaiter` | fd 就绪 `signal()`；注销 fd 或替换等待者 `signalTerminal()` | `signalCancel()` 经 `attach(…, LeafEdge{})`，恢复时抛出 `CancelledException` | `disarm`，注销监听 | `IOContextTest.UnregisteringTheSocketReportsAPendingWaitAsNotReady`、`IOContextTest.ReadinessRacingCancelResumesExactlyOnce`、`SuspensionMatrixTest.FdReadyMidCancelEndsCancelled`、`SuspensionMatrixTest.FdReadyUnregisteredReportsNotReadyWithoutThrowing` |
| `Job::join` | Job 结算（`settle`，只生效一次） | `signalCancel()`，只结束 join，Job 不受影响 | 析构结算为取消并释放等待者；等待者销毁后不再恢复 | `JobTest.JoinReturnsNormallyWhenTheBodySucceeds`、`JobTest.CancellingAJoinerStopsWaitingWithoutCancellingTheJob`、`SuspensionMatrixTest.JoinReleasedByDestroyingTheJobEndsNormally`、`SuspensionMatrixTest.JoinMidCancelEndsCancelledAndLeavesTheJobRunning` |
| `JoinAwaiter` | 最后一个子任务结束（`retireChild`） | `signalCancel()`，子任务不受影响 | scope 析构只取消、分离，等待者仍由 `core` 保持有效 | `CancellationPropagationTest.CancelAllWakesAJoinThatIsAlreadySuspended`、`CancellationPropagationTest.ScopeDestroyedWhileJoinIsSuspendedDoesNotDangle`、`StructuredConcurrencyTest.CancelledJoinStopsWaitingWithoutCancellingChildren` |
| `CoWhenAll` / `CoWhenAny` 等待者 | 全部子任务结束或首个完成者 | 上游令牌经子任务的挂接令牌到达子任务，子任务以取消结束，等待者抛取消 | 析构取消 `when_all` 的组令牌；挂起中的生成器被关闭时同样展开 | `StructuredConcurrencyTest.FailureRacingCompletionRethrowsOnlyAfterEveryChildFinishes`、`WhenAnyTest.WinnerThrowsIsPropagated`、`StructuredConcurrencyTest.ClosingAGeneratorParkedInWhenAllCancelsItsChildren`、`TokenContractTest/WhenAll_*.*`、`TokenContractTest/WhenAny_*.*` |
| `Task` 的 `co_await`（子协程） | 子协程结束，经 continuation 恢复父协程 | 取消令牌沿 `co_await` 继承 | 父协程销毁立即销毁其帧并逐层展开挂起的子协程，子协程结束后不恢复已销毁的父协程；任务销毁后取消仍安全，悬空回调已注销 | `CancellationPropagationTest.DeepChainInheritsParentToken`、`CancellationPropagationTest.CancelAfterSuspendedTaskDestroyedIsSafe`、`ChainDestroyTest.*` |
| 线程停止时的已接受任务 | 线程退出前执行 | 不适用 | 停止时排空，之后投递被拒绝 | `ThreadSchedulerTest.StopRunsEveryAcceptedTaskBeforeReturning`、`ThreadSchedulerTest.WorkPostedByADrainedTaskIsRefused` |
| 线程停止时的定时器 | 不再启动 | 不适用 | 未到期的取消；已到期但未运行的不启动 | `ThreadSchedulerTest.StopCancelsTimerWhoseCallbackIsQueuedBehindWork`、`TaskThreadTest.DelayTaskCancelledWhenThreadStops` |

不变量：

- 挂起点的终态由 `ResumeGate` 的状态机仲裁，`Signaled`、`Resuming`、`Disarmed` 不得重复恢复。
- 执行器拒绝的恢复转交回退执行器；回退执行器也拒绝时终止进程，因为续体不能被丢弃。
- 生产者或持有者销毁后，挂起的等待者不会被唤醒，它的帧随持有者释放。这是约定，不是遗漏。

### 8.18 Job：根任务的生命周期

`coro::Job<T>`（`Job.hpp`）持有一个操作的根 `Task`，是 HTTP 流与 HLS 流取消与释放网络协程的句柄。

- `makeJob(task, options)` 创建未启动的 Job，`launch(task, options)` 等于创建后调用 `start()`。`options.token` 的子令牌绑定到根协程（`Job::cancel()` 与析构取消它，不影响 `options.token`），`options.executor` 绑定执行域。空 `Task`，以及已启动（包括已完成）的 `Task`，在登记前抛出 `std::logic_error`。
- `start()` 幂等。有执行域时在其上派发首次恢复，执行器拒绝派发则以 `CancelledException` 结算，协程体不运行；没有执行域或执行域为内联时，首次恢复在调用线程同步运行。
- `cancel()` 取消 Job 自己的令牌，协程体在下一个挂起点停止。
- `waitFor(d)` 在条件变量上最多等待 `d`，不轮询，返回是否已结算。若在 Job 的执行域上调用，协程体无法运行，只能等到超时。
- `co_await job.join()` 挂起直到结算，不重新抛出 Job 的失败。等待者自身被取消时抛出 `CancelledException`，Job 不受影响。多个等待者都会被唤醒。
- `result()` 结算后返回值或重新抛出失败；结算前抛出 `std::runtime_error`。
- 结算只生效一次。协程体正常结束、取消、执行器拒绝与析构都经同一个结算点。
- 析构取消并分离，不等待协程体。未结算的 Job 以 `CancelledException` 结算，因此等待者不会永久挂起。仍在等待不可取消挂起点的协程体被分离：它由 `JobState` 持有，被唤醒后运行到结束并在结束时自行释放（`JobTest.ADetachedBodyReleasesItsFramesWhenItEnds`），与 `AsyncScope` 的子协程一致（见 8.12）。库内的叶子挂起点都经 `attach(…, LeafEdge{})` 登记令牌，组合等待经子任务的令牌被取消，因此分离体只可能在未绑定令牌的自定义 awaiter 上、且事件源永不触发时永久保留（见 8.20）。从未启动的 Job，以及被执行器拒绝派发的 Job，其协程体随 Job 销毁，不被保留（`JobTest.ARefusedJobReleasesItsBodyWhenDestroyed`）。
- 移动后源对象为空：`isDone()` 为真，`join()` 抛出 `std::logic_error`。移动赋值先取消并分离被替换的 Job。
- 线程：`start`、`cancel`、`join` 与析构由所有者线程调用；`isDone` 与 `waitFor` 可从任意线程调用。
- 流：`HttpStream` 与 `HLSStream` 以 Job 持有网络协程。取消时先取消请求与 Job，再以有限超时调用 `waitFor` 等待（HLS 使用 `kWaitSettleTimeout`）；超时只记录日志，不再轮询 `isDone()`。

### 8.19 挂起点契约矩阵

每类挂起点按同一组场景验证：挂起前取消、挂起中取消、已完成不挂起、挂起中销毁、终态通知（仅适用于会被事件源终止的挂起点）、执行器拒绝。不适用的格子在测试文件中注明原因：

- `yield` 总是先挂起并在零延迟后恢复，无法确定性地构造“已完成不挂起”与“拒绝派发”，改为取消与恢复的竞态用例，要求恰好一个结果。
- 定时器的终态通知不适用：停止调度器取消定时器，等待者不会被唤醒，这是 8.17 的既定行为，由拥有者通过令牌结束等待。
- `CompletionAwaiter` 的生产者销毁后等待者不被唤醒，这是 8.17 的约定，不计入终态通知。

基础层矩阵见 `test/base/SuspensionMatrixTest.cpp`，fd 层矩阵见 `test/http/SuspensionMatrixTest.cpp`。

### 8.20 ExternalWaitBase：单门叶子挂起点的统一协议

`ExternalWaitBase<Derived, Source, Result>`（`ExternalWaitBase.hpp`）是 `TimerAwaiter`、`YieldAwaiter`、`CompletionAwaiter::Waiter`、`JoinAwaiter` 与 `FdReadyAwaiter` 的公共基类。派生类只提供四项：

- `ready()`：快速路径，为真时不挂起。
- `start(gate)`：登记事件源，返回 RAII 的 `Source`；返回空值表示事件已经发生，帧继续运行。
- `finish()`：恢复后映射结果。
- `static std::string_view cancelMessage()`：`CancelledException` 的文本。

`enterExternalWait` 负责门（`ResumeGate`）、传输租约与外部等待标记；基类再以 `attach(…, LeafEdge{})` 登记令牌，并负责 `arm()` 与恢复的仲裁。`Source` 析构即撤销登记：定时回调、fd 续体与截止时间定时器、完成等待列表中的位置。析构时先撤销门再析构 `Source`，因此没有任何路径能恢复一个正在销毁的帧。`JoinAwaiter` 的登记保留在作用域的 join 列表中直到结算，不需要撤销。

`FdReadyAwaiter` 的两个事件源（fd 续体与可选的截止时间定时器）共用同一个门：就绪唤醒恢复帧，截止时间到达以 `signalTerminal()` 报告未就绪。`withCancellation` 与 `CancellableAwaiter` 已删除，取消只来自协程自身的令牌。

`CompositeWaitBase<Derived>`（`CompositeWaitBase.hpp`）是 `WhenAllAwaiter` 与 `WhenAnyAwaiter` 的公共基类，与 `ExternalWaitBase` 对称：叶子等一个外部事件，组合者等一组子任务。基类的 `await_suspend` 经 `enterExternalWait` 建门并登记为外部门（与叶子共用同一入口），然后调用派生类的 `start(gate, inherited, awaitingDomain)`，最后 `arm()`。派生类不接触 promise：`inherited` 是等待者协程的令牌（可能为空），`awaitingDomain` 是等待者的执行域。与叶子不同，等待者自己的令牌不会唤醒这个等待：令牌只向下传给子任务，等待者只由子任务结束唤醒，子任务因取消而结束时，等待者以取消异常恢复。基类与派生类析构时先撤销门，再取消组令牌。

### 8.21 HTTP 内部的令牌边界

HTTP 层内部不再逐层传递令牌参数。规则：

- 入口（`HttpRequest::asyncExecute` / `streamingExecute`、`HttpProcessor::process` / `processStreaming`、`Http1Session::executeEvents`、`openConnection`、`DnsResolver::resolveAsync`）保留带令牌的重载。它在返回未启动的 `Task` 或生成器之前用 `attach(SpawnEdge{}, 来源…)` 挂接一次（根形，令牌取来源的子令牌；两个来源时合并），之后靠 `co_await` 与生成器 `next()` 的继承获得令牌。生产代码不再调用 `setCancellationToken`，它只供测试夹具使用；对包装协程，手写绑定不满足守卫（见 8.13）。
- 协程内部查询取消用 `co_await coro::cancellation_requested()`，不挂起，只读取当前帧的令牌。
- `Http1Session` 的会话关闭令牌作为生成器自身的令牌绑定，与消费者令牌取 link；会话令牌在调用 `executeEvents` 时取快照。
- 无令牌的重载不得向下传入新建的 `CancellationToken{}`：它永远不会被取消，轮询会看不到外部取消。


