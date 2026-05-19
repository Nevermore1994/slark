# slark HTTP 模块深度审查报告

## 总体评价

该 HTTP 模块架构设计清晰，采用协程 + 事件驱动的异步 IO 模型，Transport 策略模式封装 TLS/Plain 差异，连接池分片锁降低竞争，整体代码质量较高。以下按严重等级列出发现的问题。

---

## 🔴 阻塞级问题

### 1. DNS 解析同步阻塞协程线程

**文件**: `HttpUtil.cpp:106`
```cpp
int32_t ret = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
```
**原因**: `getaddrinfo` 是阻塞调用，在协程执行线程上调用会阻塞整个事件循环。对于移动网络环境 DNS 解析可能耗时数秒。  
**建议**: 将 DNS 解析移至线程池执行，或采用异步 DNS 库（如 c-ares），并实现 Happy Eyeballs (RFC 8305) 并发尝试 IPv4/IPv6。

### 2. PlainTransport 未处理 EINTR

**文件**: `AsyncSocket.h:44-55`
```cpp
ssize_t write(const void* data, size_t size) noexcept override {
    return ::write(fd_, data, size);
}
bool needsRetry(ssize_t result) const noexcept override {
    return result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
}
```
**原因**: `needsRetry` 仅检查 `EAGAIN/EWOULDBLOCK`，未处理 `EINTR`。当收到信号（如 SIGALRM/debugger attach）时 `write`/`read` 返回 -1 且 `errno == EINTR`，当前代码会将其视为 `BrokenPipe`/`ConnectionReset` 直接断连。  
**建议**: 在 `needsRetry` 中增加 `errno == EINTR` 判断，或在 `write`/`read` 内层使用 `retryOnInterrupt` wrapper（该 wrapper 已定义于 `Socket.hpp:45` 但未被使用）。

### 3. HTTP Header 无大小限制

**文件**: `HttpResponseParser.cpp:18`
```cpp
buffer_.append(reinterpret_cast<const char*>(data), size);
```
**原因**: `processHeaders()` 等待找到 `\r\n\r\n` 才解析，恶意服务器可发送无限长 header 耗尽内存。当前仅有 `kMaxTrailerSize`(16KB) 限制 trailer，而主 header 无保护。  
**建议**: 增加 `kMaxHeaderSize`（建议 64KB）限制，在 `feed()` 或 `processHeaders()` 入口检查 `buffer_.size()` 超限即 `fail()`。

---

## 🟡 应修问题

### 4. TLSTransport::write size 溢出为 int32_t

**文件**: `AsyncSocket.cpp:486`
```cpp
int32_t result = SSL_write(sslPtr_.get(), data, static_cast<int32_t>(size));
```
**原因**: `SSL_write` 的 `len` 参数为 `int`，当 `size > INT_MAX` 时静默截断导致数据不完整。虽然上层通常分块发送不太可能超过 2GB，但这是不安全的隐式截断。  
**建议**: 发送前 `size = std::min(size, static_cast<size_t>(INT_MAX))`，或 assert 保护。

### 5. SSL_shutdown 未处理双向关闭

**文件**: `AsyncSocket.cpp:510-515`
```cpp
void TLSTransport::shutdown() noexcept {
    if (sslPtr_) {
        SSL_shutdown(sslPtr_.get());
        sslPtr_.reset();
    }
}
```
**原因**: `SSL_shutdown` 可能返回 0（表示发送了 close_notify 但未收到对方的），RFC 5246 建议等待对方回复。在连接池回收场景下，单次 shutdown 可能导致复用时对端发现未完成握手。  
**建议**: 对于非紧急关闭的情况，可调用两次 `SSL_shutdown` 或检查返回值确认双向关闭完成。对紧急关闭场景（timeout/error）当前行为可接受。

### 6. 连接池复用未检测半关闭 (half-close)

**文件**: `ConnectionPool.cpp:106`
```cpp
if (connInfo->isValid && connInfo->socket && connInfo->socket->isValid()) {
    // ...
    return std::move(connInfo->socket);
}
```
**原因**: `isValid()` 仅检查 `socket_ != kInvalidSocket`，未通过 `recv(MSG_PEEK)` 或 `poll` 检测对端是否已关闭连接。在 idle timeout 期间对端可能已发送 FIN，复用此连接会立即收到 RST。  
**建议**: 在 `tryReuse` 中对取出的 socket 执行 `poll(POLLIN, 0)`，若可读则尝试 `recv(MSG_PEEK)`，返回 0 说明已关闭，应丢弃。

### 7. Url 解析不处理 scheme 大小写归一化

**文件**: `Url.cpp:48`
```cpp
scheme = schemeView;
```
**原因**: RFC 3986 §3.1 规定 scheme 大小写不敏感。输入 `HTTP://` 或 `HtTp://` 时 `isHttp()` 比较 `scheme == "http"` 会返回 false，导致连接失败。  
**建议**: 解析时将 scheme 统一转为小写 (`std::transform` + `tolower`)。

### 8. RedirectPolicy 缺少 `//host` 降级检查

**文件**: `RedirectPolicy.h:105`
```cpp
if (location.starts_with("//")) {
    result.append(url.scheme);
    result.append(":");
    result.append(location);
    return result;
}
```
**原因**: 协议相对路径 `//` 继承原始 scheme，但未检查是否存在 https→http 降级风险。若原始 URL 是 https，且 location 为 `//evil.com/...` 指向 http 站点，虽然 scheme 会继承为 https，但攻击者可利用此做域名跳转（信息泄漏：cookie/auth header 发送到第三方）。  
**建议**: 对跨域重定向（host 变化）考虑剥离敏感 header（Authorization、Cookie）。

### 9. asyncReceiveAll 的 buffer 无限增长风险

**文件**: `AsyncSocket.cpp:339`
```cpp
if (newSize > kMaxReceiveBufferSize) {
    break;
}
```
**原因**: `kMaxReceiveBufferSize = 64MB`。这是一个较大上限，对于不可信服务端返回巨量数据的场景，64MB 仍可能构成内存压力（移动设备）。且 break 后并无 error 标记，调用方无法区分正常结束与截断。  
**建议**: 超限时应设置错误标记或写日志告警；考虑允许调用方通过参数配置上限。

---

## 💭 可优化项

### 10. ALPN 未配置

**文件**: `SSLManager.cpp:47-62`
```cpp
auto* context = SSL_CTX_new(TLS_client_method());
// ...未调用 SSL_CTX_set_alpn_protos
```
**原因**: 未设置 ALPN 扩展。部分 CDN（如 Cloudflare）可能根据 ALPN 协商决定行为。若未来需支持 HTTP/2 则必须配置 ALPN。  
**建议**: 添加 `SSL_CTX_set_alpn_protos(context, "\x08http/1.1", 9)` 明确声明协议。

### 11. ConnectionPool cleanup 使用 std::thread 而非协程

**文件**: `ConnectionPool.cpp:382`
```cpp
cleanupThread_.emplace([this] {
    cleanupIdleConnections();
    cleanupInProgress_.store(false);
});
```
**原因**: 每次清理启动一个新线程（虽然用 `optional<thread>` 复用了变量），线程创建销毁有开销。且 `tryScheduleCleanup` 中先 `join` 前一个线程可能阻塞协程的 `acquire` 调用路径。  
**建议**: 使用定时器/协程任务调度清理，避免阻塞 acquire 路径。

### 12. buildRequestString 未对非默认端口添加 Host 头端口

**文件**: `HttpUtil.cpp:280`
```cpp
result.append("Host: ").append(url.host).append(kCRLF);
```
**原因**: RFC 7230 §5.4 规定非默认端口时 Host 头应包含端口号（如 `Host: example.com:8080`）。当前实现仅输出 host 不含 port。  
**建议**: 当端口非 80/443 时追加 `":" + port`。

### 13. Header injection 防护可加强

**文件**: `HttpUtil.cpp:283-285`
```cpp
if (key.find('\r') != std::string::npos || key.find('\n') != std::string::npos ||
    value.find('\r') != std::string::npos || value.find('\n') != std::string::npos) {
    continue;
}
```
**原因**: 当前已做基本 CRLF 过滤（良好实践），但静默丢弃可能导致调试困难。  
**建议**: 可添加日志告警标记被过滤的 header，帮助排查注入尝试。

### 14. 跨平台兼容性：仅 POSIX socket

**文件**: `Socket.hpp` 全文、`AsyncSocket.cpp:11-14`
```cpp
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
```
**原因**: 当前代码仅支持 POSIX 平台。Type.h 注释提到 Android 目标，但无 Windows (Winsock2) 支持。若有 Windows 计划需要较大重构。  
**建议**: 如确认仅面向 POSIX（macOS/Linux/Android/iOS），可忽略。建议在 CMakeLists 或文档中明确平台范围。

---

## 总结

| 等级 | 数量 | 关键项 |
|------|------|--------|
| 🔴 阻塞 | 3 | DNS 阻塞、EINTR 未处理、Header 无限增长 |
| 🟡 应修 | 6 | SSL_write 溢出、半关闭检测、scheme 归一化等 |
| 💭 优化 | 5 | ALPN、cleanup 线程、Host 端口、跨平台注释 |

**亮点**:
- Transport 策略模式 + 协程异步 IO 设计干净
- SSL_VERIFY_PEER + SNI + hostname 验证三件套齐全
- RedirectPolicy 拒绝 https→http 降级（防 MITM）
- Header injection 过滤、chunked trailer 大小限制、ScopeGuard 确保资源回收
- 连接池分片锁 + LRU 淘汰 + 空闲超时清理

建议优先修复 🔴 级别的三个问题，尤其是 EINTR 处理和 DNS 阻塞——前者在生产环境中被信号中断的概率不低（尤其 Android debugger），后者在弱网场景会直接冻结事件循环。
