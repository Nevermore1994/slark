# slark/src/base 基础设施层代码审查报告

## 整体架构印象

**设计模式与强项**：
- 自研 `Thread` 封装了 task queue + repeat task + timer 调度的单线程事件循环，设计清晰
- MPSC 队列 + Channel 构建了良好的生产者-消费者抽象
- `Synchronized` 基于 C++23 deducing this 的统一锁接口设计优雅
- 时间轮 (`TimerPool`) 采用二级轮 + overflow 的经典架构，性能可预期
- 协程层（`Coroutine<T>`、`CompletionAwaiter`）的 CAS 状态机设计正确
- 整体采用 `NonCopyMovable` 约束、`std::expected` 错误传播、`MoveOnlyFunction` 等现代 C++ 范式

**薄弱点**：
- `Clock` 类跨线程使用全 relaxed 序，存在撕裂风险
- `Channel` 的 `Sender` 析构函数存在无锁访问共享状态
- `ThreadPool::clear()` 的取消机制可能导致 UB
- `EventLoop` 在 macOS 下 pipe 的 write end 被立即关闭，wakeup 机制失效

---

## 关键问题

### 🔴 阻塞性问题

**1. Channel Sender 析构中的数据竞争**
`Channel.hpp:188`
```cpp
~Sender() {
    if (!channel_->isClosed_) {  // 无锁读取
        done();
    }
}
```
`isClosed_` 在其他地方通过 mutex 保护写入，但此处无锁读取构成 data race (UB)。多线程同时析构 Sender 或与 Receiver 侧并发 done() 时可触发。

**建议**：改为 `if (!channel_->isDone()) { done(); }` 或直接无条件调用 `done()`（done() 内部已有幂等保护）。

---

**2. EventLoop macOS wakeup 机制完全失效**
`coroutine/EventLoop.cpp:39-46`
```cpp
int32_t pipeFds[2];
if (::pipe(pipeFds) == 0) {
    wakeupFd_ = pipeFds[0]; // read end
    ::close(pipeFds[1]);    // write end 立即关闭！
}
```
pipe 的 write end 被立即关闭，后续 `stop()` 中使用 `EVFILT_USER` 唤醒而非写 pipe，但 `wakeupFd_` 指向的 read end 从未被注册到 kqueue 也未被使用——这段 pipe 代码是无效残留。`wakeup()` 方法体为空（无实现），意味着外部线程无法从阻塞的 `poll()` 中唤醒 EventLoop。

**建议**：删除无效 pipe 代码，`wakeup()` 应使用 `EVFILT_USER` 实现，或保留 pipe 并注册到 kqueue。

---

**3. Clock 类全 relaxed 序在多线程场景下有撕裂风险**
`Clock.cpp:43-56` (`pause()`) 和 `Clock.cpp:66-78` (`setSpeed()`)
```cpp
auto last = lastUpdated_.load(std::memory_order_relaxed);
auto pts = pts_.load(std::memory_order_relaxed);
// ... 计算 ...
pts_.store(newPts, std::memory_order_relaxed);
lastUpdated_.store(now, std::memory_order_relaxed);
```
读取 `pts_` 和 `lastUpdated_` 之间无任何同步，若另一个线程正在 `setTime()`/`time()`，两个 atomic 的组合值可能不一致（非原子性地读取多变量）。严格来说不是 UB（每个 atomic 单独合法），但会导致**时间跳变或回退**。

**建议**：如果 Clock 确实只在单线程使用（如 player 主线程），建议补充注释 `// 仅限单线程调用`。如果需要多线程安全，应使用 mutex 或 `seq_cst`。

> 💭 **疑似刻意设计**：播放器场景中 Clock 常由单一控制线程操作，relaxed 序可能是性能优化。建议补充线程安全契约注释。

---

### 🟡 应修复问题

**4. ThreadPool::clear() 取消回调中 move 后使用 packagedTask**
`ThreadPool.hpp:217-225`
```cpp
.cancel = [packagedJob]() {
    auto placeholder = std::make_shared<std::packaged_task<ReturnType()>>(...);
    *packagedJob = std::move(*placeholder);  // 替换原 packaged_task 的内容
    (*packagedJob)();
},
```
`std::packaged_task` 的 move-assignment 后，原 `future` 绑定的 shared state 被释放。调用 `(*packagedJob)()` 设置的是新 shared state，但用户持有的 `future` 指向旧 state（已无效）。调用 `future.get()` 会抛出 `std::future_error` 或表现未定义。

**建议**：使用 `std::promise` 直接设置异常，或在 clear 时直接丢弃 cancel 回调（让 future 永远 pending 直到 packaged_task 析构时自动 broken_promise）。

---

**5. ThreadPool::workThread 中 activeCount_ 检查的竞态**
`ThreadPool.hpp:184-186`
```cpp
--activeCount_;
std::lock_guard<std::mutex> lock(mutex_);
if (jobs_.empty() && activeCount_ == 0) {
    allJobsDone_.notify_all();
}
```
`--activeCount_` 在锁外执行。两个线程可能同时执行 `--activeCount_` 后都看到 `activeCount_ == 0`，这本身不是 bug，但更微妙的是：线程 A `--activeCount_` 后、获锁前，线程 B 可能 submit 了新任务且 `activeCount_` 被重新递增——此时 A 获锁后仍会错误地 notify `allJobsDone_`，而 `waitForAll()` 的谓词 `jobs_.empty() && activeCount_ == 0` 在 notify 时可能已不成立。由于 `waitForAll` 有谓词保护，不会导致错误返回，但 notify 是多余的。

**建议**：将 `++activeCount_` / `--activeCount_` 都移入锁内。

---

**6. Sender::emplace 缺少 notify**
`Channel.hpp:198-204`
```cpp
template <typename... Args>
inline auto emplace(Args&&... args) -> bool {
    if (!channel_->isClosed_) {
        std::lock_guard<std::mutex> lock(channel_->mutex_);
        channel_->messages_.emplace_back(std::forward<Args>(args)...);
        return true;  // 缺少 cond_.notify_one()
    }
    return false;
}
```
Receiver 可能在 `cond_.wait()` 中阻塞，emplace 后未唤醒会导致**丢失唤醒**。

**建议**：添加 `channel_->cond_.notify_one()` 并将 `isClosed_` 检查移入锁内。

---

**7. Sender::emplace 的 isClosed_ 无锁前置检查构成 data race**
`Channel.hpp:198`
```cpp
if (!channel_->isClosed_) {  // 无锁读取 bool
```
同问题 1，`isClosed_` 在 mutex 保护下修改，此处无锁读取是 C++ 内存模型下的 UB。

**建议**：移入锁内检查，或将 `isClosed_` 改为 `std::atomic<bool>`。

---

**8. ThreadManager::reportRunInfo 锁外读取 threadInfos_.size()**
`ThreadManager.cpp:107`
```cpp
LogI("ThreadManager report now:{}, live size:{}", now.point(), threadInfos_.size());
```
此行在 mutex 外读取 `threadInfos_.size()`，构成 data race。

**建议**：在锁内获取 size 后再 log。

---

### 💭 可优化

**9. MPSCQueue 析构时不安全假设**
`MPSCQueue.hpp:60-68`
析构函数假设所有 producer 已停止写入。如果 producer 仍有 in-flight 的 push（`tail_.exchange` 已完成但 `prev->next.store` 尚未执行），析构会泄漏该 Node。

> 疑似刻意设计：使用契约保证析构前所有 producer 已停止。建议补充注释。

---

**10. ScopeGuard 使用 std::function 有额外堆分配**
`ScopeGuard.h:46`
```cpp
std::function<void()> fn_;
```
ScopeGuard 通常用于热路径（如每帧析构保护），`std::function` 的类型擦除开销不理想。

**建议**：使用模板化 `ScopeGuard<F>` 或 `MoveOnlyFunction`。

---

**11. LogOutput thread_local 生命周期风险**
`LogOutput.cpp:84-89`
```cpp
LogOutput::ThreadBuffer& LogOutput::getThreadBuffer() noexcept {
    thread_local ThreadBuffer tls;
    if (tls.owner != this) [[unlikely]] { tls.owner = this; }
    return tls;
}
```
若 LogOutput 单例析构早于某些线程退出，`ThreadBuffer::~ThreadBuffer()` 中 `flush()` 调用 `owner->submitEntries()` 时 owner 已析构。`alive_` 的 acquire 检查可以防护，但线程退出顺序在 static 析构期间不确定。

> 疑似刻意设计：通过 `alive_` flag 做了最佳努力保护。建议使用 `std::unique_ptr<LogOutput>` + `call_once`（已做到）并确保主线程最后退出。

---

**12. SPSCRingBuffer::reset() 非原子清零**
`SPSCRingBuffer.hpp:53-56`
```cpp
void reset() noexcept {
    readPos_.store(0, std::memory_order_release);
    writePos_.store(0, std::memory_order_release);
}
```
在 SPSC 语义下，reset 只能由 producer 或 consumer 单方调用，但此处无任何契约说明。若两端同时操作会导致数据错乱。

> 疑似刻意设计：reset 仅在 stop/重置流水线时由控制线程调用。建议补充 `// 调用方需保证无并发读写` 注释。

---

**13. Coroutine 右值 co_await 句柄泄漏风险**
`coroutine/Coroutine.hpp:263`
```cpp
return Awaiter{std::exchange(handle_, nullptr)};
```
若 `co_await` 表达式被异常中断（如 promise 的 `unhandled_exception` 在 `initial_suspend` 前），句柄所有权已转移到 Awaiter 但 Awaiter 未管理其生命周期——不过标准保证 Awaiter 在 `await_suspend` 返回后由 promise 管理 continuation，实际不会泄漏。无需修复，但可增加注释。

---

**14. _detail::closedHandler 是 ODR-violation 隐患**
`Channel.hpp:286-288`
```cpp
namespace _detail {
auto closedHandler = [] { throw ...; };
}
```
在 header 中定义非 inline 变量（lambda 推导为 `const auto` 但非 inline），多个 TU include 会导致多重定义。

**建议**：添加 `inline` 关键字。

---

## 总结评分

| 维度 | 评价 |
|------|------|
| 线程安全 | 整体较好，Channel 的无锁 isClosed_ 访问和 Clock 的 relaxed 组合读取需关注 |
| 内存安全 | 优秀，RAII 覆盖完整，unique_ptr 所有权清晰 |
| 现代 C++ | 积极使用 C++23 特性（deducing this、expected、concepts），代码风格统一 |
| 数据结构 | MPSC/SPSC/时间轮实现正确，内存序使用规范 |
| 异常安全 | ThreadPool 有 try-catch 保护，ScopeGuard 设计合理 |
| 跨平台 | EventLoop 的 macOS wakeup 实现有缺陷需修复 |

**整体评价**：代码质量在开源项目中处于较高水平，架构清晰，现代 C++ 运用得当。核心并发原语（MPSC、SPSC、TimerWheel）实现正确。主要问题集中在 Channel 的几处无锁访问和 EventLoop macOS 平台适配上。
