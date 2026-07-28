# Phase 4 实施文档

> 基于 `docs/coroutine_phase4.md` 设计文档，逐阶段实施计划。

---

## 当前代码概览

### 框架层（`src/base/coroutine/`）

| 文件 | 行数 | 职责 |
|------|------|------|
| `Coroutine.hpp` | 443 | `Coroutine<T>` 主类型、`PromiseBase`、`CoroutineAwaiter`、`then/and_then/or_else` |
| `CoroutineControlBlock.hpp` | 63 | `CoroutineControlBlock` 协程帧所有者、`recursive_mutex` resume |
| `SharedCoroutineHandle.hpp` | ~120 | 3 态 CAS 状态机 (`Registering/Signaled/Scheduled`) |
| `CoroutineScheduler.hpp` | 110 | `ExecutorFunc` typedef、`TimerScheduler` 单例、`scheduleVia` 兜底 |
| `Executor.h` | 47 | `inlineExec`、`onThreadPool` 等 executor 工具函数 |
| `Awaitable.hpp` | 251 | `TimerAwaiter`、`CompletionAwaiter<T>`、`registerCancelForAwaiter` |
| `AsyncIOOperation.hpp` | ~200 | `AsyncIOOperation<T>` CRTP 基类、IOState CAS、timeout |
| `EventLoop.h` | ~80 | `EventLoop` 声明 |
| `EventLoop.cpp` | ~180 | fd 注册/回调、dispatchEvent(SharedCoroutineHandle) |
| `CancellationToken.h` | ~80 | `CancellationToken` + `CancellationTokenRegistration` |
| `AsyncGenerator.hpp` | ~400 | `AsyncGenerator<T>`、promise |
| `WhenAll.hpp` | ~120 | `WhenAllAwaiter` |
| `WhenAny.hpp` | ~100 | `WhenAnyAwaiter` |
| `WithTimeout.hpp` | ~60 | `WithTimeoutAwaiter` |
| `AsyncScope.hpp` | ~100 | `AsyncScope` + spawn |
| `Coroutine.hpp` (别名) | - | `namespace slark { using Task = Coroutine; }` |

### 消费层

| 文件 | 使用的协程 API |
|------|---------------|
| `src/core/HttpStream.h/cpp` | `Coroutine<void>` 成员、`resume()` |
| `src/core/HLSStream.h/cpp` | `Coroutine<void>` 成员、`resume()` |
| `src/http/HttpRequest.h/cpp` | `Coroutine<HttpResponse>`、`co_await` |
| `src/http/HttpProcessor.h` | `Coroutine<ResponseResult>` |
| `src/http/AsyncSocket.h/cpp` | `FdReadyAwaiter`、`IOContext::instance()` |
| `src/http/DnsResolver.h/cpp` | `CompletionAwaiter`、`Coroutine` |
| `src/http/IOContext.h` | `FdReadyAwaiter`、`IOContext::instance()` |
| `src/http/CallbackBridge.hpp` | `shared_from_this`、`Coroutine` |

### 编译器相关

| 文件 | 影响 |
|------|------|
| `src/base/MoveOnlyFunction.hpp` | `ExecutorFunc = MoveOnlyFunction<void(shared_ptr<SharedCoroutineHandle>)>` 将被替换 |
| `src/base/NonCopyable.h` | `Task<T>` 用 `NonCopyable` 基类 |
| `src/base/CMakeLists.txt` | 文件 glob，无需修改 |

---

## Phase 4A：Task 核心（新建，不删旧代码）

### 4A.1 新建 `src/base/coroutine/Task.hpp`

替换文件：无（新建，与 `Coroutine.hpp` 共存）

包含内容（按顺序）：

```
1. IExecutor 抽象类 + InlineExecutor + inlineExecutor() 全局函数 + FunctionExecutor
2. class EventLoop 前向声明
3. TaskPromiseBase
   - continuation_    : std::coroutine_handle<>（raw handle）
   - executor_        : IExecutor* = &inlineExecutor()（永不为空）
   - eventLoop_       : EventLoop* = nullptr（可选）
   - completed_       : atomic<bool> = false
   - FinalAwaiter     : await_suspend 返回 continuation_（对称转移）
   - inheritContext   : 从父 promise 复制 executor_ + eventLoop_
   - setContinuation  : 设置 continuation_
   - takeContinuation : 取走并置空
4. TaskPromise<T> : public TaskPromiseBase
   - value_ / exception_
   - get_return_object → Task<T>
   - return_value / unhandled_exception / getResult
5. TaskPromise<void> : public TaskPromiseBase（同上，无 value_）
6. Task<T> class（move-only，继承 NonCopyable）
   - handle_ : coroutine_handle<promise_type>
   - 构造：接收 handle
   - 移动构造/赋值：exchange handle_
   - ~Task() : if (handle_) handle_.destroy()
   - getHandle() : copy
   - releaseHandle() : exchange → nullptr
   - operator co_await() && → TaskAwaiter<T>（&& 限定，防止 lvalue co_await）
   - scheduleVia(IExecutor&) : 设置 executor_ + executor.execute(resume)
7. TaskAwaiter<T>
   - await_ready : !handle_ || handle_.done()
   - await_suspend : setContinuation + inheritContext + return handle_（对称转移）
   - await_resume : getResult()
8. ScheduleOnAwaiter + scheduleOn(IExecutor&) 工厂
9. CurrentExecutorAwaiter + co_current_executor() 工厂
```

**关键差异对比（旧 Coroutine vs 新 Task）**：

| 方面 | Coroutine | Task |
|------|-----------|------|
| 协程帧生命周期 | `shared_ptr<CoroutineControlBlock>` | Task 析构 `handle_.destroy()` |
| continuation_ | `shared_ptr<SharedCoroutineHandle>` | `std::coroutine_handle<>` |
| final_suspend | `takeContinuation → notify → scheduleContinuation` | `return continuation_`对称转移 |
| executor 类型 | `ExecutorFunc`（`MoveOnlyFunction`） | `IExecutor*`（抽象类指针） |
| executor 设置 | `setExecutor(fn)` | `promise.executor_ = &exec` |
| co_await 语义 | `operator co_await() const&` + `&&` | 仅 `operator co_await() &&` |
| 取消传播 | 自动复制 `cancelToken_` | 无（per-awaiter `co_withCancellation`） |
| resume 锁 | `recursive_mutex` in ControlBlock | 无锁 |
| 恢复调度 | `scheduleVia（executor, control->resume）` 的复杂链 | 直接 `executor->execute` 或 `h.resume()` |

### 4A.2 新建 `test/base/TaskTest.cpp`

```cpp
// 在 test/base/CMakeLists.txt 中注册（或确保 glob 收录）

// ============ 分组 1: TaskBasic（4 tests）============
TEST(TaskBasic, CreateAndResume) {
    // 创建 Task<int>、resume、getHandle().done()
}
TEST(TaskBasic, MoveOwnership) {
    // move 后源 handle_ 为 nullptr，~Task() 不 destroy
}
TEST(TaskBasic, MovedFromDestructorSafe) {
    // move 后源对象的析构安全（handle_ = nullptr）
}
TEST(TaskBasic, DestroyWithoutResume) {
    // 未 resume 的 Task 析构安全（handle_.destroy()）
}

// ============ 分组 2: TaskCoAwait（3 tests）============
TEST(TaskCoAwait, CoAwaitValue) {
    // co_await Task<T> 获取值
}
TEST(TaskCoAwait, CoAwaitVoid) {
    // co_await Task<void>
}
TEST(TaskCoAwait, CoAwaitAlreadyDone) {
    // await_ready 返回 true，跳过 await_suspend
}

// ============ 分组 3: TaskSymmetricTransfer（3 tests）============
TEST(TaskSymmetricTransfer, ParentChildChain) {
    // 父 co_await 子 → 对称转移，无 executor 绕行
}
TEST(TaskSymmetricTransfer, ThreeLevelChain) {
    // A → co_await B → co_await C，三层对称转移
}
TEST(TaskSymmetricTransfer, ExceptionPropagation) {
    // 子抛出异常，父 await_resume 重抛
}

// ============ 分组 4: TaskInheritContext（4 tests）============
TEST(TaskInheritContext, ExecutorPropagation) {
    // 父 scheduleVia(ioExec) → 子 inheritContext 拿到 ioExec
}
TEST(TaskInheritContext, EventLoopPropagation) {
    // 父 promise.eventLoop_ = &loop → 子继承
}
TEST(TaskInheritContext, ChildOverride) {
    // 子在 inheritContext 后自己设 executor_，覆盖父的值
}
TEST(TaskInheritContext, ThreeLayerPropagation) {
    // 祖父 → 父 → 子，三层都继承
}

// ============ 分组 5: TaskScheduleOn（4 tests）============
TEST(TaskScheduleOn, SwitchExecutor) {
    // scheduleOn(cpuExec) → promise.executor_ == &cpuExec
}
TEST(TaskScheduleOn, ChildrenInheritAfterSwitch) {
    // 父亲 scheduleOn(cpuExec) → co_await child → child.executor_ == &cpuExec
}
TEST(TaskScheduleOn, SwitchBack) {
    // scheduleOn(io) → scheduleOn(cpu) → scheduleOn(io)
}
TEST(TaskScheduleOn, DefaultIsInline) {
    // 未 scheduleVia 的 Task，co_current_executor 返回 inlineExecutor
}

// ============ 分组 6: TaskCoCurrentExecutor（2 tests）============
TEST(TaskCoCurrentExecutor, ReturnsReference) {
    // co_await co_current_executor() 返回 IExecutor&
}
TEST(TaskCoCurrentExecutor, AfterScheduleOn) {
    // scheduleOn(exec) 后 co_current_executor 返回该 exec
}

// ============ 分组 7: TaskCoWithCancellation（4 tests）============
TEST(TaskCoWithCancellation, CancelTimer) {
    // co_withCancellation(token, TimerAwaiter(10s)) → token.cancel() → 协程恢复
}
TEST(TaskCoWithCancellation, NoCancelNormalComplete) {
    // co_withCancellation(token, TimerAwaiter(1ms)) → 定时器触发 → 正常恢复
}
TEST(TaskCoWithCancellation, CancelAfterAwaiterDestroyed) {
    // awaiter 析构后 token.cancel() → alive-flag 抑制回调
}
TEST(TaskCoWithCancellation, ConcurrentCancelAndComplete) {
    // 两个线程同时取消和完成 → alive-flag 仲裁
}

// ============ 分组 8: TaskMoveSemantics（2 tests）============
TEST(TaskMoveSemantics, DestroyMovedFrom) {
    // move 后的 Task 析构不 double-destroy
}
TEST(TaskMoveSemantics, ResumeNullTask) {
    // resume 空 handle 不 crash
}

// ============ 分组 9: TaskException（2 tests）============
TEST(TaskException, PropagateThroughCoAwait) {
    // 子 throw → 父 await_resume throw
}
TEST(TaskException, UnhandledExceptionInPromise) {
    // promise.unhandled_exception → getResult 重抛
}

// 总计：28 tests
```

### 4A.3 验证标准

```bash
# Phase 4A 完成标志
cd build && cmake --build . --target Base_test
./test/base/Base_test --gtest_filter="Task*"

# 确认旧测试仍然通过（共生验证）
./test/base/Base_test --gtest_filter="Coroutine*"
```

- [ ] `TaskTest` 全部 28 个测试通过
- [ ] `CoroutineTest` 全部 35 个测试通过（旧框架未受影响）
- [ ] `Task.hpp` 零外部依赖（仅 `<coroutine>`、`<atomic>`、`<functional>`）

---

## Phase 4B：Awaiter 重写

### 4B.1 重写 `src/base/coroutine/Awaitable.hpp`

**改变**：311 行 → ~200 行

删除内容：
- `registerCancelForAwaiter` 模板函数（SharedCoroutineHandle 依赖）
- `TimerAwaiter` 基于 `SharedCoroutineHandle::notify/suspend` 的实现
- `CompletionAwaiter` 基于 `SharedCoroutineHandle` 的 finish

新增内容：
```cpp
// 1. TimerAwaiter（alive-flag 版）
//    - 使用 weakFlag + aliveFlag 替代 SharedCoroutineHandle
//    - scheduleCallback 直接 lambda 持有 handle_
//    - cancel 回调：不调用 handle_.resume()，仅取消定时器
//    - CancellationToken 回调加 alive-flag 保护：token.registerCallback([aliveFlag]{...})

// 2. CompletionAwaiter<T>（alive-flag 版，合并 T 和 void 特化）
//    - aliveFlag_ 替代 SharedCoroutineHandle
//    - finish() 处理 Init→Completed 和 Waiting→Completed 两路径
//    - await_ready 精确检查 State::Completed
//    - 取消回调：设置 cancelledFlag，不 resume（由 IO 唤醒）
//    - CancellationToken 回调加 alive-flag 保护

// 3. CancellableWrapper<Awaiter> + co_withCancellation
//    - 转发内部 awaiter 的 await_suspend 返回值
//    - alive-flag 保护取消回调
//    - cancelReg_ 在 await_resume 中自动注销
```

### 4B.2 重写 `src/base/coroutine/AsyncIOOperation.hpp`

**改变**：~200 行 → ~120 行

```
- IOState CAS 保持不变
- handle_ 从 shared_ptr<SharedCoroutineHandle> → std::coroutine_handle<>
- aliveFlag_ 新增（替代 SharedCoroutineHandle 的 cancelWait）
- finisher() → 直接用 handle_.resume()，不走 executor
- cancelOperation() → aliveFlag 检查 + fd 取消
- CancellationToken 回调：[this, aliveFlag] 保护
```

### 4B.3 修改 `src/base/coroutine/EventLoop.h` + `EventLoop.cpp`

**EventLoop.h 新增**：

```cpp
class EventLoop {
public:
    // 全局默认（替代 IOContext 单例）
    static EventLoop* getDefault() noexcept;
    static void setDefault(EventLoop* loop) noexcept;

    // RegistrationToken 中的 handle 改为 std::coroutine_handle<>
    // 新增 aliveFlag 参数用于 dispatchEvent 中检查
    RegistrationId registerContinuation(Registration reg);
    void unregisterContinuation(RegistrationId id);

private:
    inline static std::atomic<EventLoop*> defaultLoop_{nullptr};
    // fdMutex_ 保护 registrations_ map
};

struct Registration {
    int32_t fd;
    FdInterest interest;
    std::coroutine_handle<> handle;       // 原 SharedCoroutineHandle
    std::shared_ptr<std::atomic<bool>> aliveFlag;  // 新增
    uint64_t generation;
};
```

**EventLoop.cpp dispatchEvent 改动**：

```cpp
// 旧：if (reg.handle->notify()) { scheduleVia(...); }
// 新：
if (reg.aliveFlag && reg.aliveFlag->load(std::memory_order_acquire)) {
    // 不再掉用 notify()，直接 resume（已持有锁，摘除了 reg）
    reg.handle.resume();
}
```

### 4B.4 修改 `src/http/IOContext.h`

**FdReadyAwaiter 改动**：三级回退 → promise 传播

```cpp
// 旧：regId_ = IOContext::instance().registerContinuation({...});
// 新：
// 优先级：explicitLoop_ > promise.eventLoop_ > EventLoop::getDefault()
// EventLoop* loop = resolveEventLoop(handle.promise());
// regId_ = loop->registerContinuation({...});
```

### 4B.5 新建/更新测试

**新建 `test/base/AwaitableTest.cpp`（15 tests）**：

```
CompletionAwaiter_PreComplete       // complete() 在 await_suspend 前调用
CompletionAwaiter_NormalComplete    // await_suspend 后 complete
CompletionAwaiter_DoubleComplete    // 重复 complete 幂等
CompletionAwaiter_Fail              // fail + 异常传播
TimerAwaiter_NormalFire             // 定时器正常触发
TimerAwaiter_CancelBeforeFire       // cancel 后定时器不触发
TimerAwaiter_CancelCallbackWithAliveFlag  // 取消回调 alive-flag 检查
AliveFlag_AfterAwaiterDestructed    // 析构后回调不 resume
AliveFlag_ConcurrentSet             // 多线程并发 set aliveFlag
CoWithCancellation_CancelTimer      // CancellableWrapper 取消定时器
CoWithCancellation_NoCancel         // 无取消正常完成
CoWithCancellation_SymmetricTransfer // 转发内部 awaiter 的 await_suspend 返回值
CancellationToken_MultiAwaiter      // 多个 awaiter 同一 token
CancellationToken_RegRAII           // cancelReg_ 析构自动 unregister
CancellationToken_CallbackAfterDestroy  // 回调在 awaiter 销毁后，aliveFlag 保护
```

**新建 `test/base/EventLoopTest.cpp`（5 tests）**：

```
EventLoop_SetGetDefault
EventLoop_FdReadyReadsPromiseEventLoop
EventLoop_ExplicitOverride
EventLoop_FallbackToDefault
EventLoop_DispatchEventWithAliveFlag
```

---

## Phase 4C：AsyncGenerator 与组合子

### 4C.1 修改 `src/base/coroutine/AsyncGenerator.hpp`

**适配 Task 模型**：
- GeneratorPromise 不再依赖 `SharedCoroutineHandle`
- `final_suspend` 改为对称转移（恢复等待的消费者）
- `next()` 返回的 awaiter 使用 alive-flag

### 4C.2 重写组合子

**`WhenAll.hpp`**：

```
- WhenAllState: continuation_ → std::coroutine_handle<>
- whenAllChild: 返回 Task<void>
- WhenAllAwaiter: childTasks_ (vector<Task<void>>) 保活
- await_suspend: IExecutor* 提取 + startTasks
- scheduleVia: IExecutor& 设置
```

**`WhenAny.hpp`**：同上模式

**`WithTimeout.hpp`**：**删除**

```cpp
// 删除原因：语义由 co_withCancellation + when_any 替代
// co_await withTimeout(task, 5s)
//   → co_await when_any(exec, task, []() -> Task<void> { co_await TimerAwaiter(5s); }())
// co_await co_withCancellation(token, TimerAwaiter(5s))
```

**`AsyncScope.hpp`**：

```
- spawn: wrapper 协程内部 try/catch + activeCount.fetch_sub + notify
- setDefaultExecutor: IExecutor&
- childTasks_: vector<Task<void>> 保活
```

### 4C.3 测试

**更新 `test/base/AsyncGeneratorTest.cpp`（49 现有测试适配）**

**更新 `test/base/WhenAllTest.cpp`**：
```
新增：IExecutor& 并行调度、无 executor 串行、析构时子任务硬取消
```

**更新 `test/base/WhenAnyTest.cpp`**：同上

**更新 `test/base/AsyncScopeTest.cpp`**：
```
新增：activeCount 递减、waitAll 超时清理、spawn 并发安全
```

---

## Phase 4D：迁移 + 删除旧代码

### 4D.1 迁移列表（按依赖顺序）

| 顺序 | 文件 | 改动 |
|------|------|------|
| 1 | `src/core/HttpStream.h` | `Coroutine<void> coroutine_` → `Task<void> task_` |
| 1 | `src/core/HttpStream.cpp` | `coroutine_.resume()` → `task_.scheduleVia(ioExecutor)` |
| 2 | `src/core/HLSStream.h` | 同上 |
| 2 | `src/core/HLSStream.cpp` | 同上 |
| 3 | `src/http/HttpRequest.h` | `Coroutine<HttpResponse>` → `Task<HttpResponse>` |
| 3 | `src/http/HttpRequest.cpp` | 适配 |
| 4 | `src/http/HttpProcessor.h` | 同上模式 |
| 4 | `src/http/HttpProcessor.cpp` | 适配 |
| 5 | `src/http/AsyncSocket.h/cpp` | 适配 FdReadyAwaiter 新签名 |
| 6 | `src/http/DnsResolver.h/cpp` | 适配 CompletionAwaiter 新签名 |
| 7 | `src/http/ConnectionPool.h/cpp` | 适配 Coroutine → Task |
| 8 | `src/http/CallbackBridge.hpp` | `shared_ptr<SharedCoroutineHandle>` → alive-flag |

### 4D.2 删除文件

| 文件 | 原因 |
|------|------|
| `src/base/coroutine/Coroutine.hpp` | 被 `Task.hpp` 取代 |
| `src/base/coroutine/CoroutineControlBlock.hpp` | 独占所有权不需要控制块 |
| `src/base/coroutine/SharedCoroutineHandle.hpp` | alive-flag 替代 3 态 CAS |
| `src/base/coroutine/WithTimeout.hpp` | `co_withCancellation` + `when_any` 替代 |
| `src/base/coroutine/Executor.h` | IExecutor 替代 ExecutorFunc |

### 4D.3 可简化的文件

| 文件 | 简化 |
|------|------|
| `src/base/coroutine/CoroutineScheduler.hpp` | 不再需要 `scheduleVia(ExecutorFunc, SharedCoroutineHandle)`；移除 `TimerScheduler` 的 `scheduleAfter` SharedCoroutineHandle 重载 |
| `src/base/MoveOnlyFunction.hpp` | 不再作为 `ExecutorFunc` 的基础类型使用 |

### 4D.4 测试迁移

| 操作 | 文件 | 测试数 |
|------|------|--------|
| **删除** | `test/base/CoroutineTest.cpp` | -35 |
| **删除** | `test/base/CoroutineContractTest.cpp` | -5 |
| **删除** | `test/base/CoroutineControlBlockTest.cpp` | -8 |
| **删除** | `test/base/CoroutineAbiTest.cpp` | -4 |
| **新建** | `test/http/TaskIntegrationTest.cpp` | +8 |
| **保留** | `test/base/CancellationTokenTest.cpp` | 无需修改 |

### 4D.5 回归检查清单

```bash
# 1. 全量基础测试
cmake --build . --target Base_test && ./test/base/Base_test

# 2. HTTP 测试
cmake --build . --target HTTP_test && ./test/http/HTTP_test

# 3. 核心集成测试
cmake --build . --target CoreIntegration_test && ./test/core/CoreIntegration_test

# 4. 模糊测试
cmake --build . --target BaseFuzz_test && ./test/fuzz/BaseFuzz_test

# 5. ASAN 泄漏检测
ASAN_OPTIONS=detect_leaks=1 ./test/base/Base_test --gtest_filter="Task*"

# 6. TSAN 数据竞争检测
TSAN_OPTIONS=history_size=7 ./test/base/Base_test --gtest_filter="Task*"
```

### 4D.6 最终文件清单

| 分类 | 文件 | 状态 |
|------|------|------|
| 框架 | `Task.hpp` | 新建 |
| 框架 | `Awaitable.hpp` | 重写 |
| 框架 | `AsyncIOOperation.hpp` | 重写 |
| 框架 | `EventLoop.h` | 修改 |
| 框架 | `EventLoop.cpp` | 修改 |
| 框架 | `CancellationToken.h` | 不变 |
| 框架 | `AsyncGenerator.hpp` | 修改 |
| 框架 | `WhenAll.hpp` | 重写 |
| 框架 | `WhenAny.hpp` | 重写 |
| 框架 | `AsyncScope.hpp` | 重写 |
| 框架 | `CoroutineScheduler.hpp` | 简化 |
| 框架 | `MoveOnlyFunction.hpp` | 不变 |
| HTTP | `IOContext.h` | 修改（FdReadyAwaiter） |
| HTTP | `HttpRequest.h/cpp` | 修改 |
| HTTP | `HttpProcessor.h/cpp` | 修改 |
| HTTP | `AsyncSocket.h/cpp` | 修改 |
| HTTP | `DnsResolver.h/cpp` | 修改 |
| HTTP | `ConnectionPool.h/cpp` | 修改 |
| HTTP | `CallbackBridge.hpp` | 修改 |
| core | `HttpStream.h/cpp` | 修改 |
| core | `HLSStream.h/cpp` | 修改 |
| 删除 | `Coroutine.hpp` | 删除 |
| 删除 | `CoroutineControlBlock.hpp` | 删除 |
| 删除 | `SharedCoroutineHandle.hpp` | 删除 |
| 删除 | `WithTimeout.hpp` | 删除 |
| 删除 | `Executor.h` | 删除 |
| 测试 | `test/base/TaskTest.cpp` | 新建（28 tests） |
| 测试 | `test/base/AwaitableTest.cpp` | 新建（15 tests） |
| 测试 | `test/base/EventLoopTest.cpp` | 新建（5 tests） |
| 测试 | `test/http/TaskIntegrationTest.cpp` | 新建（8 tests） |
| 测试 | 旧 Coroutine 测试文件 ×4 | 删除 |
| 测试 | `AsyncGeneratorTest.cpp` | 修改 |
| 测试 | `WhenAllTest.cpp` | 重写 |
| 测试 | `WhenAnyTest.cpp` | 重写 |
| 测试 | `AsyncScopeTest.cpp` | 重写 |

---

## 各 Phase 验证门禁

| Phase | 测试数 | 门禁条件 |
|-------|--------|---------|
| 4A | +28 new, 35 existing = 63 | TaskTest 全通过 + CoroutineTest 全通过（共生） |
| 4B | +20 new = 83 total | AwaitableTest + EventLoopTest 全通过 |
| 4C | AsyncGeneratorTest(49) + WhenAll/Any/Scope 重写 | 组合子测试全通过 |
| 4D | -52 delete, +8 new = 39 final | 全部测试通过 + ASAN/TSAN clean |
