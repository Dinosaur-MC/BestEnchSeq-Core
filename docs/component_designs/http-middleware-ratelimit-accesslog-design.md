# HTTP 中间件链 + 限流 + 访问日志 — 设计文档

> 状态：**已批准**（2026-08-11，brainstorming 逐节确认）
> 关联：`components/http`（HttpServer/Connection/Router）；上线规划：Nginx 前置（自动 TLS）
> 目标：为传输层引入通用 middleware 机制，托管限流（每 IP + 全局令牌桶，无锁）与
> Combined 格式访问日志（INFO 级），为未来 CORS/鉴权等中间件留位。

## 1. 背景与目标

### 1.1 为什么

- HTTP Server 目前只有"路由 + fallback"两级分发，无通用前置/后置处理机制；
- 服务规划上线（Nginx 前置）：需要限流防御与符合惯例的访问日志；
- 访问日志要**符合 HTTP Server 惯例**（Apache/nignx 家族格式）、INFO 级；
- 服务器设计原则：连接零锁状态机、loop 线程永不阻塞、无全局互斥——限流器必须
  遵守同一哲学（无锁构造），不引入新的全局争用点。

### 1.2 范围

**本期实现**：
- `components/http` 通用 middleware 链（函数式，Express/Koa 风格）；
- 每 IP 令牌桶限流 + 全局原子令牌桶，429 + `Retry-After` + 错误信封；
- 可信代理（Nginx）下的客户端 IP 解析（`X-Forwarded-For` 信任策略）；
- Combined Log Format 访问日志（INFO 级，异步 Logger 输出）。

**本期不做（文档标注，上线前另行设计）**：
- IPv6（listener 当前 AF_INET）；
- 限流阈值热更新（启动期配置）；
- 解析级 400/413 的访问日志记录（需 Connection 日志钩子，属过度设计）；
- CORS / 鉴权中间件（机制就绪后按需添加）。

## 2. 架构

### 2.1 Middleware 链（零侵入传输路径）

`Connection::process` 拿到的仍是一个 `Handler`（`HttpResponse(const HttpRequest&)`）。
`HttpServer::run()` 启动时把 `routes + fallback` 包成最终 handler，再按注册序**逆序
嵌套** middleware，形成单一 dispatch 闭包。连接层、解析器、Reactor 不改。

```
Connection::process → dispatch 闭包（middleware 链）
  → access_logger(req, next)          // 最外层：调 next 后在返回路上记录
      → rate_limiter(req, next)       // 可短路：不调 next → 直接 429
          → final: routes + fallback（→ WebModule.dispatch）
```

- 中间件在 loop 线程**同步**执行（与 dispatch 同线程），无跨线程队列；
- `use()` 与 `set_handler` 同约束：**run 前注册**，run 内只读。

### 2.2 新组件（`src/domain/interface/components/http/`）

| 文件 | 内容 |
|---|---|
| `Middleware.h` | `Next`/`Middleware` 类型 + `client_addr()` 声明 |
| `Middleware.cpp` | `client_addr()` 实现（可信代理解析） |
| `RateLimiter.h/.cpp` | `RateLimitConfig` + `make_rate_limiter(cfg)` |
| `AccessLog.h/.cpp` | `make_access_logger()`（`set_access_log` 开关） |
| `HttpServer.h/cpp` | 新增 `use(Middleware)` + `set_access_log(bool)`；`run()` 组装链 |

### 2.3 API 形态

```cpp
using Next = std::function<HttpResponse(const HttpRequest&)>;
using Middleware = std::function<HttpResponse(const HttpRequest&, const Next&)>;

void HttpServer::use(Middleware m);          // run 前注册，按注册序嵌套
void HttpServer::set_access_log(bool on);    // 默认开启；内部 use(make_access_logger())

server.use(web::make_access_logger());
server.use(web::make_rate_limiter(web::RateLimitConfig{...}));
```

## 3. 客户端 IP 解析（限流 key 与访问日志共用）

```cpp
/// 真实客户端 IP：trust_forwarded 且直连对端 ∈ trusted_proxies 时，
/// 取 X-Forwarded-For 最右条目（nginx 追加的那个）；否则退回 socket 对端。
/// trust_forwarded=false（默认）时 XFF 完全被忽略——防伪造头绕过每 IP 限流。
std::string client_addr(const HttpRequest& req);
```

- `HttpRequest` 新增 `std::string remote_addr`：`Connection` 构造时 `getpeername`
  捕获（AF_INET，IPv4）。单元测试直调时为空串 → `client_addr` 返回 `-`。
- 安全语义：`trust_forwarded=false` 时 XFF 不读（直连攻击者无法伪造绕过）；
  `true` 时仅当直连对端 ∈ 可信名单（默认 `{"127.0.0.1"}`）才采信 XFF 最右条目。
- Nginx 部署：`trust_forwarded=true`（对端恒为 nginx）。

## 4. 限流器（无锁构造）

### 4.1 每 IP 桶：固定容量 lock-free 哈希表

- 容量 `RateLimitConfig::slots`（默认 16384，构造期一次性分配 ≈ 512KB，**零热路径堆
  分配**）；
- 槽位：`{atomic<uint64_t> hash, atomic<double> tokens, atomic<int64_t> last_refill_ns}`；
- 线性探测 + CAS 更新；表满时 CAS 替换 idle 槽（被换桶视作新桶放行一次）；
- **内存有界**：表大小固定 → 无限 IP 洪水不涨内存（生产 DoS 防线）。

### 4.2 全局桶：原子令牌桶

`atomic<double> tokens + atomic<int64_t> last_refill_ns`，CAS refill；每请求 1-2 次 CAS。

### 4.3 算法与 429

```
请求 → 每 IP 桶：tokens = min(cap, tokens + Δt × rps)
     → tokens ≥ 1 ? tokens -= 1 放行 : 429
     → 全局桶（同算法）任一不足即 429
```

429 响应：`429 Too Many Requests` + `Retry-After: <ceil((1−tokens)/rps)>` 秒 +
信封 body `{"error":"rate limit exceeded","code":"RATE_LIMITED"}`（对齐现有 {error,code} 惯例）。

### 4.4 配置与默认值

```cpp
struct RateLimitConfig {
    bool enabled = false;            // 默认关闭：现有行为零变化，HTTP 服务（besq serve）显式开启
    double ip_rps = 20;              // 每 IP 补令牌速率（次/秒）
    size_t ip_burst = 40;
    double global_rps = 200;
    size_t global_burst = 400;
    size_t slots = 16384;            // 每 IP 桶表容量（内存有界）
    bool trust_forwarded = false;
    std::vector<std::string> trusted_proxies = {"127.0.0.1"};
};
```

默认关闭是刻意的：现有测试（8 并发、close-storm 32 连发）在开启限流下必然触发
429；默认关闭保证老测试与现有部署行为不变。上线时由装配点显式开启。

## 5. 访问日志（Combined Log Format，INFO 级）

```
127.0.0.1 - - [11/Aug/2026:10:15:30 +0800] "GET /api/status HTTP/1.1" 200 512 "http://localhost:18789/" "Mozilla/5.0 ..."
```

| 字段 | 来源 |
|---|---|
| 远端 IP | `client_addr(req)`（与限流 key 同一函数） |
| ident / authuser | 恒 `-` |
| 时间戳 | `[%d/%b/%Y:%H:%M:%S %z]`（`localtime_r`/`localtime_s` 线程安全版本） |
| 请求行 | `"METHOD path HTTP/1.1"`（path 原始未解码） |
| status | `resp.status`（含 429/404/405/500） |
| bytes | `resp.body.size()`（CLF `%b` 惯例：响应 **body** 字节数、不含头部，与 Apache/nginx 一致）；`is_stream` → `-`；0 字节 → `-` |
| Referer / User-Agent | `req.header(...)` 缺省 `-`；**日志注入消毒**（控制字符替换 `_`） |

行为：
- 输出 `Logger::instance().info(...)` → INFO 级 → 全局异步日志消费者链（控制台 +
  文件消费者，测试/未来持久化经 `add_consumer` 注册）；
- **429 也记**（nginx 惯例：一切到达的请求都记）；
- **异常兜底**：`next()` 抛异常时记 500 后重抛（防御性）；
- **解析级 400/413 不记**（解析阶段被 Connection 拦截，不进入 middleware；与 nginx
  的差异本期接受）；
- 开关 `set_access_log(bool)`，默认开启。

## 6. 数据流与错误处理

```
Connection::process → 链 → 响应 ← 逐层返回 → Connection 写线
```

- 429：限流器直接构造响应，不经过路由；
- 异常：access_log 捕获重抛（记 500）；限流器内部不抛（纯数据操作）；
- 坏请求：Connection 现有 400/413 路径，不进 middleware。

## 7. 测试

### 7.1 新增 `test_middleware`（components/http 层）

1. **链序与短路**：注册序嵌套；不调 `next` 即短路；`use()` 运行期约束；
2. **限流语义**（低阈值显式配置，如 `ip_rps=100, ip_burst=2`）：
   - 突发内放行、超突发 429（status/`Retry-After`/信封断言）；
   - 桶恢复：等待补令牌后 200；
   - 每 IP 隔离；全局桶叠加（多 IP 同时超全局 → 全 429）；
   - `trust_forwarded` 语义：false 时伪造 XFF 不绕过；true 且对端可信时采信最右条目；
3. **访问日志**：CLF Combined 行精确断言（字段顺序、`-` 占位、时间戳正则
   `\[\d{2}/[A-Z][a-z]{2}/\d{4}:\d{2}:\d{2}:\d{2} [+-]\d{4}\]`）；`is_stream` 字节 `-`；
   缺 Referer/UA → `-`；控制字符注入消毒断言；
4. **异常兜底**：middleware 抛异常 → 记 500 行后重抛。

### 7.2 `test_web_integration` 增补（真实 socket 端到端）

- 低阈值限流 → 连发超限收到线格式 429 + `Retry-After`；恢复后 200；
- 访问日志默认开启下全用例仍绿（现有断言数与行为不变）。

### 7.3 既有测试零影响

限流默认关闭；访问日志只写日志不改行为。`test_http_server` / `test_web_integration`
现有断言数与行为不变。

## 8. 上线边界（本期不做，文档标注）

- IPv6（listener 目前 AF_INET）；
- `X-Forwarded-For` 之外的代理头（`X-Real-IP` 等）——nginx 默认追加 XFF，够用；
- 限流阈值热更新（启动期配置；如需运行时调整，走重启或后续 `/api/settings` 通道）；
- 解析级 400/413 访问日志（需 Connection 日志钩子）；
- **默认访问日志的策略对齐（Nginx 前置任务内做）**：run() 自动装配的默认访问日志用默认 `ClientAddrPolicy`（不信任 XFF），而限流器经 `trust_forwarded=true` 采信 XFF——部署时两者会分叉（日志记代理 IP、限流 key 记真实客户端）。逃生口已存在（`set_access_log(false)` + `use(make_access_logger(policy))` 自装同策略版），但需手动同步两个旋钮。Nginx 部署任务中应加 `HttpServer::set_access_log_policy(...)` 或让 HTTP 服务装配策略一致的日志器，消除隐性不一致。
