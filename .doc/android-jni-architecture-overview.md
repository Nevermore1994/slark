## Android JNI 架构介绍

### 文档目标

本文档用于介绍 Slark Android SDK 中 `src/platform/Android/sdk/src/main/cpp/jni` 模块的架构组成、职责边界与运行流程。内容以“现状说明”为主，不包含设计优劣评判。

### 模块在平台层中的位置

该目录提供 C++ 与 Java/Kotlin 间的 JNI 基础设施，主要能力包括：

- JVM 生命周期接入
- 线程 `JNIEnv*` 获取与自动 detach
- `jclass/jmethodID` 缓存
- 签名构建与参数编组
- 泛型化 Java 调用封装
- 原生日志回流

---

## 文件组成与分层

目录：`src/platform/Android/sdk/src/main/cpp/jni`

```text
jni/
├── JNI.h
├── JNIBase.h
├── JNICache.h
├── JNICache.cc
├── JNICall.h
├── JNIEnvGuard.h
├── JNIInit.cc
├── JNIMarshal.h
├── JNIMarshal.cc
├── JNISignature.h
├── LogBridge.h
└── LogBridge.cc
```

可分为 6 个子层：

1. **入口与汇总层**：`JNI.h`、`JNIInit.cc`
2. **基础对象层**：`JNIBase.h`、`JNIEnvGuard.h`
3. **缓存层**：`JNICache.h/.cc`
4. **签名与编组层**：`JNISignature.h`、`JNIMarshal.h/.cc`
5. **调用封装层**：`JNICall.h`
6. **桥接业务层**：`LogBridge.h/.cc`

---

## 启动与生命周期

### `JNIInit.cc`

提供标准 JNI 入口：

- `JNI_OnLoad(JavaVM*, void*)`
  - 保存 `JavaVM*` 到全局
  - 校验 `JNI_VERSION_1_6` 环境
- `JNI_OnUnload(JavaVM*, void*)`
  - 清理全局引用（如 `gApplicationContext`）
  - 置空全局 `JavaVM*`

`JNIBase.h` 中 `getJavaVM()` 提供对当前全局 JVM 指针的统一访问。

---

## 基础对象与线程环境

### 1) `JNIReference<T>`（`JNIBase.h`）

`JNIReference<T>` 对局部引用进行 RAII 封装：

- 析构时自动 `DeleteLocalRef`
- 支持 move 语义
- `detach()` 可转移所有权

类型别名：

- `ClassReference`
- `ObjectReference`
- `MethodReference`
- `StringReference`

其中 `JNIReference<jmethodID>` 提供了特化：主要用于异常检查与方法 ID 持有。

### 2) `JNIEnvGuard`（`JNIEnvGuard.h`）

`JNIEnvGuard` 处理 native 线程与 JVM 的 attach/detach：

- 构造：`GetEnv`，必要时 `AttachCurrentThread`
- 析构：若本对象 attach 过线程，则自动 `DetachCurrentThread`

用于保证跨线程调用 JNI 时环境获取一致。

---

## 缓存层：`JNICache`

文件：`JNICache.h`、`JNICache.cc`

`JNICache` 是类与方法缓存中心，单例形式暴露：`JNICache::instance()`。

### 核心能力

- `getClass(env, className)`
  - 查找/创建类缓存
  - 缓存保存为 `jweak`，每次使用转成本地引用

- `getMethod(...)` / `getStaticMethod(...)`
  - 缓存实例方法和静态方法的 `jmethodID`
  - 以 `classTag + methodName + signature` 为 key

- `getEnumField(...)`
  - 获取 Java 枚举静态字段对象

- `clear()`
  - 遍历删除 `jweak` 引用并清空缓存

### 并发控制

- `classCache_` 与 `methodCache_` 分离
- 使用 `std::shared_mutex` 读写锁
  - 读路径先查
  - miss 后进入写路径更新

---

## 签名与编组层

### 1) `JNISignature.h`

提供 JNI 签名常量与拼接函数：

- 基础签名常量：`Sig::Int`、`Sig::Long`、`Sig::String` 等
- `makeObject(className)`：构建对象签名
- `makeArray(type)`：构建数组签名
- `makeSignature(ret, args...)`：拼接方法签名

### 2) `JNIMarshal.h/.cc`

负责 C++ 与 JVM 类型转换：

- `FromJVM`
  - `str(JNIEnv*, jstring)`
  - `bytes(JNIEnv*, jbyteArray)`
  - `ints(JNIEnv*, jintArray)`

- `ToJVM`
  - `str(JNIEnv*, std::string_view)`
  - `bytes(JNIEnv*, const DataBuffer&)`
  - `ints(JNIEnv*, const std::vector<int32_t>&)`
  - `naluBytes(...)`（构建带起始码的 NALU 字节数组）

---

## 调用封装层：`JNICall`

文件：`JNICall.h`

`JNICall` 提供模板化调用入口，统一实例/静态方法调用与字段访问。

### 调用入口

- `callStatic<R>(env, cls, name, args...)`
- `call<R>(env, obj, cls, name, args...)`
- `create(env, cls, args...)`（调用构造函数）

### 字段访问

- `getStaticField<R>(...)`
- `getField<R>(...)`

### 内部机制

- `sigOf<T>()`：编译期类型到 JNI 签名映射
- `methodSig<R, Args...>()`：组合方法签名
- `toArg(env, v)`：参数转换
- `JObjectWithSig{jobject, sig}`：参数方向，为 jobject 参数指定运行时精确签名
- `ReturnSig{sig}`：返回值方向，为 call/callStatic 指定返回值精确签名
- `invoke / invokeStatic`：分类型调用 JNI API
- 返回值用 `std::optional<R>` 或 `ObjectReference` 统一承载

---

## 业务桥接示例：`LogBridge`

文件：`LogBridge.h`、`LogBridge.cc`

`LogBridge` 提供 Android -> native 的日志桥接：

- Java 层调用 `nativeLog(level, message)`
- C++ 入口 `Java_com_slark_jni_LogBridge_nativeLog(...)`
- 转发到 `slark::JNI::logFromAndroid(...)`
- 最终映射到 native 日志系统（`FLOG_LEVEL`）

该文件展示了“Java native 方法导出 + 参数提取 + 本地业务调用”的最小闭环。

---

## 典型调用链（示意）

```text
Java/Kotlin call
  -> JNI exported symbol (extern "C")
  -> JNIEnv / class/method resolve (JNICache)
  -> arg marshal (JNIMarshal + toArg)
  -> method invoke (JNICall)
  -> result convert / return
```

---

## 一句话总结

当前 Android JNI 架构可概括为：以 `JNIInit` 建立 JVM 生命周期入口，以 `JNIBase/JNIEnvGuard` 管理环境与引用，以 `JNICache + JNICall + JNIMarshal + JNISignature` 形成“可缓存、可复用、模板化”的调用基础设施，并通过 `LogBridge` 等桥接点承接具体平台交互。