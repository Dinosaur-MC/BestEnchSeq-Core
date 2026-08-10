# HTTP Middleware 链 + 限流 + 访问日志 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 components/http 传输层加入函数式 middleware 链，托管无锁限流器（每 IP + 全局令牌桶）与 Combined Log Format 访问日志（INFO 级），并接入 GUI。

**Architecture:** `HttpServer::run()` 把 routes+fallback 包成最终 Handler，按注册序逆序嵌套 `Middleware`（`std::function<HttpResponse(const HttpRequest&, const Next&)>`）形成单一 dispatch 闭包——`Connection::process` 零改动。限流器用固定容量 lock-free 哈希表（16B 原子 `BucketState` 单 CAS refill+consume）+ 原子全局桶；访问日志为最外层中间件，记录一切到达的请求（含 429/404）。客户端 IP 解析共用 `client_addr(req, policy)`（可信代理 XFF 策略）。

**Tech Stack:** C++20 / Clang / CMake+Ninja / 现有 `components/http`（零第三方依赖）、`common/log` 异步 Logger、`tests/framework/test_framework.h`、Windows `build/` + WSL `build-wsl/` 双构建树。

**规格来源:** `docs/domain_designs/http-middleware-ratelimit-accesslog-design.md`（已批准）

---

## File Map

**新建：**
- `src/domain/interface/components/http/Middleware.h` — `Next`/`Middleware`/`ClientAddrPolicy`/`client_addr` 声明（纯头）
- `src/domain/interface/components/http/Middleware.cpp` — `client_addr` 实现（XFF 可信代理解析）
- `src/domain/interface/components/http/RateLimiter.h` / `RateLimiter.cpp` — `RateLimitConfig` + 无锁限流器 + `make_rate_limiter`
- `src/domain/interface/components/http/AccessLog.h` / `AccessLog.cpp` — `make_access_logger`（CLF Combined）
- `tests/domain/interface/test_middleware.cpp` — 新测试目标（单元 + 真实 socket）

**修改：**
- `src/domain/interface/components/http/HttpCommon.h` — `HttpRequest` 加 `remote_addr`/`version`
- `src/domain/interface/components/http/HttpCommon.cpp` — `reason_phrase` 加 `case 429: "Too Many Requests"`
- `src/domain/interface/components/http/HttpParser.cpp` — 解析后填充 `out.version`
- `src/domain/interface/components/http/Socket.h/.cpp` — `sock_peer_addr(int fd)`
- `src/domain/interface/components/http/Connection.h/.cpp` — 捕获 `_remote`，process 时填 `req.remote_addr`
- `src/domain/interface/components/http/HttpServer.h/.cpp` — `use()`/`set_access_log()` + run 组装链
- `tests/domain/interface/test_connection.cpp` — 元数据捕获用例
- `tests/domain/interface/CMakeLists.txt` — `besq_test_interface(test_middleware test_middleware.cpp)`
- `src/gui/main.cpp` — 显式开启限流

> 注：`components/http/*.cpp` 由 `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` 收集（`src/domain/interface/CMakeLists.txt:4`），新 .cpp 自动入编；测试目标需手动注册。

**编译/运行约定**：单测迭代用 Windows 树 `build/`（命令统一 `cmake --build build --target <t> -j 8` 后 `./build/bin/<t>.exe`）；Task 8 全量验证双平台（Windows `build/` + WSL `build-wsl/`）。

---

### Task 1: HttpRequest 元数据（remote_addr + version）

请求需要携带对端 IP（限流 key / 访问日志客户端 IP）与线格式版本（访问日志请求行）。连接层在构造时用 `getpeername` 捕获一次，解析器填充版本。

**Files:**
- Modify: `src/domain/interface/components/http/HttpCommon.h:43-59`（HttpRequest）
- Modify: `src/domain/interface/components/http/Socket.h`、`Socket.cpp`
- Modify: `src/domain/interface/components/http/Connection.h`、`Connection.cpp`
- Modify: `src/domain/interface/components/http/HttpParser.cpp:96`（版本校验处）
- Test: `tests/domain/interface/test_connection.cpp`

- [ ] **Step 1: 写失败测试**

在 `tests/domain/interface/test_connection.cpp` 的 `namespace { ... }` 内（`StubRouter` 定义之后）追加：

```cpp
// ---------------------------------------------------------------------------
// HttpRequest 元数据捕获：remote_addr（对端 IP，限流 key / 访问日志 IP 字段）
// 与 version（线格式版本，访问日志请求行）。
// ---------------------------------------------------------------------------
static void test_request_meta_capture() {
    TcpListener l;
    expect(l.listen("127.0.0.1", 0), "listen");
    int client = sock_connect("127.0.0.1", l.bound_port());
    expect(client >= 0, "connect");
    int fd = l.accept();
    expect(fd >= 0, "accept");
    set_nonblocking(fd);
    set_nonblocking(client);
    Connection conn(fd, "id-meta");
    std::string remote, version;
    Connection::Router router = [&](const HttpRequest& req) {
        remote = req.remote_addr;
        version = req.version;
        return HttpResponse::json(200, "OK", R"({})");
    };
    expect(sock_send(client, "GET /ping HTTP/1.1\r\nHost: x\r\n\r\n"), "send");
    for (int i = 0; i < 100 && remote.empty(); ++i) {
        conn.process(router);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    expect(remote == "127.0.0.1", "remote_addr is the peer address");
    expect(version == "HTTP/1.1", "version captured from request line");
    sock_close(client);
    conn.close();
}
```

在 TEST_CASE 的调用列表（`test_keepalive_two_requests();` 之前）加一行：`test_request_meta_capture();`

- [ ] **Step 2: 运行验证失败**

Run: `cmake --build build --target test_connection -j 8`
Expected: 编译失败 —— `no member named 'remote_addr' in 'web::HttpRequest'`。

- [ ] **Step 3: 实现字段与捕获**

`HttpCommon.h` 的 `HttpRequest` 中，`bool expect_continue = false;` 之后插入：

```cpp
    /// 对端 IPv4 地址（getpeername，Connection 构造时捕获）。限流 key 与访问
    /// 日志客户端 IP 字段的来源；单元测试直调/非连接上下文为空串。
    std::string remote_addr;
    /// 请求行 HTTP 版本（"HTTP/1.1"/"HTTP/1.0"，解析器填充）——访问日志请求行。
    std::string version;
```

`Socket.h`（`bool set_send_buffer(...)` 声明之后）加：

```cpp
/// Peer IPv4 address of a connected socket ("127.0.0.1"); "" on failure.
/// Used for rate-limit keys and access-log client IPs.
std::string sock_peer_addr(int fd);
```

`Socket.cpp`（`sock_close` 实现之后）加：

```cpp
std::string sock_peer_addr(int fd) {
    if (fd < 0) return "";
    sockaddr_in peer{};
#ifdef _WIN32
    int len = sizeof(peer);
#else
    socklen_t len = sizeof(peer);
#endif
    if (::getpeername(native(fd), reinterpret_cast<sockaddr*>(&peer), &len) != 0)
        return "";
    char buf[INET_ADDRSTRLEN] = {0};
    if (!inet_ntop(AF_INET, &peer.sin_addr, buf, sizeof(buf)))
        return "";
    return buf;
}
```

（`Socket.cpp` 顶部已包含 winsock2/ws2tcpip（Windows）与 arpa/inet.h/netinet/in.h（POSIX），`native`/`sockaddr_in` 均可用。）

`Connection.h` 私有成员区（`int _fd;` 之前）加：

```cpp
    std::string _remote;             // 对端 IP（getpeername，构造时捕获一次）
```

`Connection.cpp` 构造函数（`touch();` 之后）加：

```cpp
    _remote = sock_peer_addr(fd);    // 对端 IP：限流 key 与访问日志 IP 字段
```

`Connection.cpp` 的 `process()` 内，`try { req.stream = shared_from_this(); }` 块之前加：

```cpp
        req.remote_addr = _remote;
```

`HttpParser.cpp` 版本校验处（`if (version != "HTTP/1.1" && version != "HTTP/1.0") return ParseResult::BadRequest;` 之后）加：

```cpp
    out.version = version;
```

- [ ] **Step 4: 运行验证通过**

Run: `cmake --build build --target test_connection -j 8 && ./build/bin/test_connection.exe`
Expected: `Results: N passed, 0 failed`（新增 2 断言）。

- [ ] **Step 5: 提交**

```bash
git add src/domain/interface/components/http/ tests/domain/interface/test_connection.cpp
git commit -m "feat(http): HttpRequest 携带 remote_addr（getpeername）与 version（解析器填充）"
```

---

### Task 2: Middleware 链（类型 + use() + run 组装）

传输层函数式中间件链：类型定义 + `HttpServer::use()` + run() 逆序嵌套组装。本任务不接入访问日志（Task 5 负责）。

**Files:**
- Create: `src/domain/interface/components/http/Middleware.h`
- Modify: `src/domain/interface/components/http/HttpServer.h`、`HttpServer.cpp:146-184`（run）
- Create: `tests/domain/interface/test_middleware.cpp`
- Modify: `tests/domain/interface/CMakeLists.txt`

- [ ] **Step 1: 写失败测试**

创建 `tests/domain/interface/test_middleware.cpp`：

```cpp
// =============================================================================
// HTTP middleware 链 + 限流 + 访问日志（components/http）
// =============================================================================
#define BESQ_TEST_MAIN
#include "domain/interface/components/http/HttpServer.h"
#include "domain/interface/components/http/Middleware.h"
#include "domain/interface/components/http/Socket.h"
#include "framework/test_framework.h"
#include <string>
#include <thread>
#include <vector>

using namespace web;

namespace {

HttpResponse json_ok() { return HttpResponse::json(200, "OK", R"({"ok":true})"); }

// ---------------------------------------------------------------------------
// 链序与短路：真实 HttpServer + 真实 socket。
// ---------------------------------------------------------------------------
TEST_CASE("test_middleware_chain") {
    HttpServer server;
    std::vector<std::string> order;
    server.use([&order](const HttpRequest& req, const Next& next) {
        order.push_back("A");
        return next(req);
    });
    server.use([&order](const HttpRequest& req, const Next& next) {
        order.push_back("B");
        return next(req);
    });
    server.set_handler(Method::Get, "/health",
                       [&order](const HttpRequest&) { order.push_back("H"); return json_ok(); });
    expect(server.start("127.0.0.1", 0), "server starts");
    std::thread srv([&] { server.run(); });
    struct ServerGuard {
        HttpServer& s;
        std::thread& t;
        ~ServerGuard() {
            if (t.joinable()) {
                s.stop();
                t.join();
            }
        }
    } guard{server, srv};

    auto get = [&](const std::string& path) {
        int c = sock_connect("127.0.0.1", server.port());
        expect(c >= 0, "client connects");
        sock_send(c, "GET " + path + " HTTP/1.1\r\nHost: x\r\n\r\n", 3000);
        std::string got;
        sock_recv(c, got, 4096, 3000);
        sock_close(c);
        return got;
    };

    auto r = get("/health");
    expect(r.find("200 OK") != std::string::npos, "handler reached through chain");
    expect(order.size() == 3 && order[0] == "A" && order[1] == "B" && order[2] == "H",
           "middlewares run in registration order before the handler");

    // 404 也经过链（链包裹 routes+fallback 整体）
    order.clear();
    auto nf = get("/nope");
    expect(nf.find("404") != std::string::npos, "404 goes through the chain");
    expect(order.size() == 2 && order[0] == "A" && order[1] == "B",
           "404 still passes through middlewares");

    // 短路：不调 next → 后链不执行
    HttpServer server2;
    std::vector<std::string> order2;
    server2.use([&order2](const HttpRequest&, const Next&) {
        order2.push_back("S");
        return HttpResponse::error(403, "FORBIDDEN", "blocked");
    });
    server2.set_handler(Method::Get, "/health",
                        [&order2](const HttpRequest&) { order2.push_back("H"); return json_ok(); });
    expect(server2.start("127.0.0.1", 0), "server2 starts");
    std::thread srv2([&] { server2.run(); });
    struct ServerGuard2 {
        HttpServer& s;
        std::thread& t;
        ~ServerGuard2() {
            if (t.joinable()) {
                s.stop();
                t.join();
            }
        }
    } guard2{server2, srv2};
    int c2 = sock_connect("127.0.0.1", server2.port());
    expect(c2 >= 0, "client2 connects");
    sock_send(c2, "GET /health HTTP/1.1\r\nHost: x\r\n\r\n", 3000);
    std::string got2;
    sock_recv(c2, got2, 4096, 3000);
    sock_close(c2);
    expect(got2.find("403") != std::string::npos, "short-circuit response 403");
    expect(order2.size() == 1 && order2[0] == "S", "downstream handler not invoked");
}

} // namespace
```

在 `tests/domain/interface/CMakeLists.txt` 中 `besq_test_interface(test_http_server test_http_server.cpp)` 之后加：

```cmake
besq_test_interface(test_middleware test_middleware.cpp)
```

- [ ] **Step 2: 运行验证失败**

Run: `cmake --build build --target test_middleware -j 8`
Expected: 编译失败 —— `no member named 'use' in 'web::HttpServer'`。

- [ ] **Step 3: 实现类型与链组装**

创建 `src/domain/interface/components/http/Middleware.h`：

```cpp
#pragma once
#include "HttpCommon.h"
#include <functional>
#include <string>
#include <vector>

namespace web {

/// middleware 链类型（Express/Koa 风格）：m(req, next) 调用 next(req) 继续链，
/// 不调用即短路（限流 429 路径）。链在 HttpServer::run() 启动时组装；
/// use() 与 set_handler 同约束：run 前注册（run 后注册对已运行服务器无效）。
using Next = std::function<HttpResponse(const HttpRequest&)>;
using Middleware = std::function<HttpResponse(const HttpRequest&, const Next&)>;

/// 客户端 IP 解析策略（限流 key 与访问日志客户端 IP 共用，见 client_addr）。
struct ClientAddrPolicy {
    /// true = 直连对端 ∈ trusted_proxies 时采信 X-Forwarded-For 最右条目
    ///（Nginx 前置部署）；false（默认）= XFF 完全忽略（防伪造头绕过限流）。
    bool trust_forwarded = false;
    std::vector<std::string> trusted_proxies = {"127.0.0.1"};
};

/// 真实客户端 IP：见 ClientAddrPolicy。req.remote_addr 为空 → 返回空串。
std::string client_addr(const HttpRequest& req, const ClientAddrPolicy& policy);

} // namespace web
```

`HttpServer.h`：`#include "Middleware.h"`（`#include "HttpCommon.h"` 之后），public 区 `void set_fallback(Handler h);` 之后加：

```cpp
    /// 注册 middleware（run 前；与 set_handler 同约束：run 内快照、只读）。
    /// 按注册序嵌套，首个注册者最外层。默认访问日志（set_access_log(true)）
    /// 位于所有用户中间件之外（见 AccessLog.h）。
    void use(Middleware m);
    /// 默认访问日志开关（默认开启；Task 5 生效）。关闭后可用
    /// use(make_access_logger(policy)) 自装自定义策略版本。
    void set_access_log(bool on);
```

`HttpServer.cpp`：
- `#include "Middleware.h"`（`#include "Connection.h"` 之后）
- `struct HttpServer::Impl` 内 `std::vector<Route> routes;` 之后加 `std::vector<Middleware> middlewares;`（Impl 定义在 .cpp，不在头文件）
- `set_fallback` 实现之后加：

```cpp
void HttpServer::use(Middleware m) {
    if (!_impl)
        _impl = std::make_unique<Impl>();
    _impl->middlewares.push_back(std::move(m));
}

void HttpServer::set_access_log(bool on) {
    if (!_impl)
        _impl = std::make_unique<Impl>();
    _impl->access_log = on;
}
```

- `struct HttpServer::Impl` 内 `std::vector<Middleware> middlewares;` 之后加 `bool access_log = true;`（**注意**：Task 5 才把它接入 run()；本任务先保持默认值，行为无变化）
- `run()` 内，现有 `Handler dispatch = [routes = impl.routes, fallback = impl.fallback](...)` 闭包之后加：

```cpp
    // middleware 链组装（run 前快照；逆序嵌套 → 注册序 = 外层→内层）。
    // Connection::process 拿到的仍是一个 Handler——传输路径零改动。
    for (auto it = impl.middlewares.rbegin(); it != impl.middlewares.rend(); ++it)
        dispatch = [m = *it, prev = std::move(dispatch)](const HttpRequest& req) {
            return m(req, prev);
        };
```

- [ ] **Step 4: 运行验证通过**

Run: `cmake --build build --target test_middleware -j 8 && ./build/bin/test_middleware.exe`
Expected: `Results: N passed, 0 failed`；`test_middleware_chain` 全绿。

- [ ] **Step 5: 提交**

```bash
git add src/domain/interface/components/http/Middleware.h src/domain/interface/components/http/HttpServer.h src/domain/interface/components/http/HttpServer.cpp tests/domain/interface/test_middleware.cpp tests/domain/interface/CMakeLists.txt
git commit -m "feat(http): 传输层 middleware 链（use() 注册 + run 逆序嵌套组装）"
```

---

### Task 3: client_addr 可信代理解析

客户端 IP 解析：默认不采信 XFF；`trust_forwarded` 且对端可信时取 XFF 最右条目。

**Files:**
- Create: `src/domain/interface/components/http/Middleware.cpp`
- Test: `tests/domain/interface/test_middleware.cpp`（新增 TEST_CASE）

- [ ] **Step 1: 写失败测试**

在 `tests/domain/interface/test_middleware.cpp` 的 `} // namespace` 之前追加：

```cpp
// ---------------------------------------------------------------------------
// client_addr：默认忽略 XFF；trust_forwarded + 可信对端时取 XFF 最右条目。
// ---------------------------------------------------------------------------
TEST_CASE("test_client_addr") {
    HttpRequest req;
    req.remote_addr = "127.0.0.1";
    req.headers.emplace_back("X-Forwarded-For", "1.2.3.4, 5.6.7.8");
    ClientAddrPolicy p;
    expect(client_addr(req, p) == "127.0.0.1", "XFF ignored by default");
    p.trust_forwarded = true;
    expect(client_addr(req, p) == "5.6.7.8", "rightmost XFF entry trusted");
    p.trusted_proxies = {"10.0.0.1"};
    expect(client_addr(req, p) == "127.0.0.1", "untrusted peer falls back to peer addr");
    p.trusted_proxies = {"127.0.0.1"};
    req.headers.clear();
    expect(client_addr(req, p) == "127.0.0.1", "missing XFF falls back to peer addr");
    req.remote_addr.clear();
    expect(client_addr(req, p).empty(), "no peer addr yields empty");
}
```

- [ ] **Step 2: 运行验证失败**

Run: `cmake --build build --target test_middleware -j 8 && ./build/bin/test_middleware.exe`
Expected: 链接失败 —— `undefined reference to client_addr`。

- [ ] **Step 3: 实现**

创建 `src/domain/interface/components/http/Middleware.cpp`：

```cpp
#include "Middleware.h"

namespace web {

namespace {

/// X-Forwarded-For 最右条目（去 OWS）；缺失/空白 → 空串。
std::string rightmost_xff(const HttpRequest& req) {
    const std::string xff = req.header("X-Forwarded-For");
    if (xff.empty())
        return "";
    const size_t comma = xff.rfind(',');
    const std::string_view last = comma == std::string::npos
                                      ? std::string_view(xff)
                                      : std::string_view(xff).substr(comma + 1);
    const size_t b = last.find_first_not_of(" \t");
    const size_t e = last.find_last_not_of(" \t");
    if (b == std::string_view::npos || e == std::string_view::npos)
        return "";
    return std::string(last.substr(b, e - b + 1));
}

} // namespace

std::string client_addr(const HttpRequest& req, const ClientAddrPolicy& policy) {
    if (req.remote_addr.empty())
        return "";
    if (policy.trust_forwarded) {
        bool trusted_peer = false;
        for (const auto& p : policy.trusted_proxies)
            if (p == req.remote_addr) {
                trusted_peer = true;
                break;
            }
        if (trusted_peer) {
            const std::string fwd = rightmost_xff(req);
            if (!fwd.empty())
                return fwd;
        }
    }
    return req.remote_addr;
}

} // namespace web
```

- [ ] **Step 4: 运行验证通过**

Run: `cmake --build build --target test_middleware -j 8 && ./build/bin/test_middleware.exe`
Expected: 全部通过（含 `test_client_addr`）。

- [ ] **Step 5: 提交**

```bash
git add src/domain/interface/components/http/Middleware.cpp tests/domain/interface/test_middleware.cpp
git commit -m "feat(http): client_addr 可信代理解析（X-Forwarded-For 信任策略）"
```

---

### Task 4: 无锁限流器（每 IP + 全局令牌桶）

固定容量 lock-free 哈希表（16B 原子桶状态单 CAS refill+consume）+ 原子全局桶；429 + Retry-After + 错误信封；`reason_phrase` 补 429。

**Files:**
- Create: `src/domain/interface/components/http/RateLimiter.h`、`RateLimiter.cpp`
- Modify: `src/domain/interface/components/http/HttpCommon.cpp:185-200`（reason_phrase）
- Test: `tests/domain/interface/test_middleware.cpp`（新增 TEST_CASE）

- [ ] **Step 1: 写失败测试**

在 `tests/domain/interface/test_middleware.cpp` 的 `} // namespace` 之前追加（并在文件头 `#include "domain/interface/components/http/Middleware.h"` 之后加 `#include "domain/interface/components/http/RateLimiter.h"`）：

```cpp
// ---------------------------------------------------------------------------
// 限流器（直接调用中间件，合成 HttpRequest——不走线，确定性测试）
// ---------------------------------------------------------------------------
TEST_CASE("test_ratelimit") {
    RateLimitConfig cfg;
    cfg.enabled = true;
    cfg.ip_rps = 1000.0;        // 1 token/ms：恢复测试毫秒级
    cfg.ip_burst = 2;
    cfg.global_rps = 100000.0;  // 全局几乎不限（叠加测试单独配置）
    cfg.global_burst = 100000;
    auto rl = make_rate_limiter(cfg);
    int served = 0;
    Next next = [&served](const HttpRequest&) {
        ++served;
        return json_ok();
    };
    HttpRequest req;
    req.remote_addr = "1.2.3.4";

    // 突发上限：burst=2 → 第 3 个 429
    for (int i = 0; i < 3; ++i)
        rl(req, next);
    expect(served == 2, "burst 2 admits first two requests");
    auto r429 = rl(req, next);
    expect(r429.status == 429, "third+ request denied");
    expect(r429.header_value("Retry-After") != "", "Retry-After header present");
    expect(r429.body.find("\"code\"") != std::string::npos, "error envelope carries code");
    expect(r429.body.find("RATE_LIMITED") != std::string::npos, "envelope code RATE_LIMITED");

    // 桶恢复：速率 1 token/ms → 5ms 后恢复
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    expect(rl(req, next).status == 200, "recovered after refill");

    // 每 IP 隔离：另一 IP 独立计桶
    HttpRequest req2;
    req2.remote_addr = "5.6.7.8";
    served = 0;
    for (int i = 0; i < 3; ++i)
        rl(req2, next);
    expect(served == 2, "per-IP buckets are independent");

    // XFF 采信：trust_forwarded 下 key = XFF 最右条目（对端 127.0.0.1 可信）
    RateLimitConfig cfg2 = cfg;
    cfg2.client_addr_policy.trust_forwarded = true;
    auto rl2 = make_rate_limiter(cfg2);
    HttpRequest reqx;
    reqx.remote_addr = "127.0.0.1";
    reqx.headers.emplace_back("X-Forwarded-For", "9.9.9.9");
    served = 0;
    for (int i = 0; i < 3; ++i)
        rl2(reqx, next);
    expect(served == 2, "XFF-based key buckets by forwarded IP");

    // 全局桶叠加：全局容量 1 → 第二个请求即 429（无论 IP）
    RateLimitConfig cfg3 = cfg;
    cfg3.global_rps = 0.001;
    cfg3.global_burst = 1;
    auto rl3 = make_rate_limiter(cfg3);
    served = 0;
    HttpRequest rA;
    rA.remote_addr = "1.1.1.1";
    HttpRequest rB;
    rB.remote_addr = "2.2.2.2";
    rl3(rA, next);
    rl3(rB, next);
    expect(rl3(rA, next).status == 429, "global bucket exhausted limits all IPs");

    // disabled：透传
    RateLimitConfig off;
    auto rlOff = make_rate_limiter(off);
    served = 0;
    expect(rlOff(req, next).status == 200, "disabled limiter passes through");
    expect(served == 1, "disabled limiter invokes next");
}
```

- [ ] **Step 2: 运行验证失败**

Run: `cmake --build build --target test_middleware -j 8`
Expected: 编译失败 —— 找不到 `RateLimiter.h`。

- [ ] **Step 3: 实现限流器**

创建 `src/domain/interface/components/http/RateLimiter.h`：

```cpp
#pragma once
#include "HttpCommon.h"
#include "Middleware.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace web {

struct RateLimitConfig {
    bool enabled = false;                       // 默认关闭：现有行为零变化，装配点显式开启
    double ip_rps = 20;                         // 每 IP 补令牌速率（次/秒）
    size_t ip_burst = 40;                       // 每 IP 桶容量（允许突发）
    double global_rps = 200;                    // 全局共享桶速率
    size_t global_burst = 400;                  // 全局桶容量
    size_t slots = 16384;                       // 每 IP 桶表容量（内存有界，固定分配）
    ClientAddrPolicy client_addr_policy;        // trust_forwarded / trusted_proxies
};

/// 每 IP + 全局令牌桶限流中间件（无锁：固定容量 lock-free 哈希表；桶状态
/// 16B 原子单 CAS refill+consume+时间推进）。超限 → 429 + Retry-After + 信封
/// {"ok":false,"error":{"code":"RATE_LIMITED",...}}。
class RateLimiter {
public:
    explicit RateLimiter(RateLimitConfig cfg);

    HttpResponse operator()(const HttpRequest& req, const Next& next);

private:
    struct BucketState {
        double tokens = 0;
        int64_t last_ns = 0;
    };
    struct Slot {
        std::atomic<uint64_t> hash{0};      // 0 = 空槽；否则 IP 的 FNV-1a 哈希
        std::atomic<BucketState> state{};   // 桶状态（单 CAS 原子更新）
    };
    static_assert(std::atomic<BucketState>::is_always_lock_free,
                  "rate limiter requires lock-free 16-byte atomics");

    /// 桶定位：命中 / 认领空槽 / 表满替换异主槽（被换 IP 下次重新认领）。
    /// 替换时新桶从零开始（首次使用满桶）。极端竞争兜底：首槽（共享一桶，
    /// 限流更严，安全方向）。
    Slot* locate(const std::string& ip);
    /// 单桶取令牌：refill + consume + last 推进一次 CAS 原子完成。
    /// false = 桶空（调用方构造 429）。首次使用（last_ns==0）视为满桶。
    static bool take(std::atomic<BucketState>& st, double rate, double cap);
    /// 429：Retry-After = ceil((1 - tokens) / rate) 秒（rate<=0 → 0）。
    static HttpResponse denied(double rate);

    std::unique_ptr<Slot[]> _table;     // slots 个槽，构造期一次性分配
    std::atomic<BucketState> _global{}; // 全局共享桶
    RateLimitConfig _cfg;
};

/// 中间件工厂。返回的 Middleware 持有 shared_ptr<RateLimiter>（RateLimiter
/// 含 unique_ptr 不可拷贝，经 shared_ptr 装箱满足 std::function 可拷贝约束）。
Middleware make_rate_limiter(RateLimitConfig cfg);

} // namespace web
```

创建 `src/domain/interface/components/http/RateLimiter.cpp`：

```cpp
#include "RateLimiter.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace web {

namespace {

/// FNV-1a 64 位；0 保留给空槽标记（哈希恰为 0 时映射为 1）。
uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h ? h : 1;
}

} // namespace

RateLimiter::RateLimiter(RateLimitConfig cfg)
    : _table(new Slot[cfg.slots > 0 ? cfg.slots : 1]), _cfg(std::move(cfg)) {}

bool RateLimiter::take(std::atomic<BucketState>& st, double rate, double cap) {
    using namespace std::chrono;
    const int64_t now = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    BucketState s = st.load(std::memory_order_relaxed);
    for (;;) {
        BucketState next = s;
        if (s.last_ns == 0) {
            next.tokens = cap;                              // 首次使用：满桶
        } else if (now > s.last_ns) {
            next.tokens = std::min(cap, s.tokens + static_cast<double>(now - s.last_ns) * 1e-9 * rate);
        }
        next.last_ns = now;
        if (next.tokens >= 1.0) {
            next.tokens -= 1.0;
            if (st.compare_exchange_weak(s, next, std::memory_order_relaxed))
                return true;
            continue;                                       // s 已被刷新，重算
        }
        st.compare_exchange_weak(s, next, std::memory_order_relaxed); // 尽力推进时间
        return false;
    }
}

HttpResponse RateLimiter::denied(double rate) {
    HttpResponse r = HttpResponse::error(429, "RATE_LIMITED", "rate limit exceeded");
    double wait_s = 0;
    if (rate > 0)
        wait_s = std::ceil(1.0 / rate);                     // 保守：至少 1 枚令牌的等待
    r.headers.emplace_back("Retry-After",
                           std::to_string(static_cast<long>(wait_s)));
    return r;
}

RateLimiter::Slot* RateLimiter::locate(const std::string& ip) {
    const uint64_t h = fnv1a(ip);
    const size_t n = _cfg.slots;
    for (size_t i = 0; i < n; ++i) {
        Slot* slot = &_table[(h + i) % n];
        uint64_t cur = slot->hash.load(std::memory_order_relaxed);
        if (cur == h)
            return slot;
        if (cur == 0) {
            if (slot->hash.compare_exchange_weak(cur, h, std::memory_order_relaxed))
                return slot;                                // 认领空槽
            continue;                                       // 他者先占，继续探测
        }
    }
    // 表满：替换第一个异主槽（仅当活跃 IP 数 > slots 时发生；被换 IP 下次
    // 请求重新认领——有限规模的必然代价，新桶满桶放行一次）。
    for (size_t i = 0; i < n; ++i) {
        Slot* slot = &_table[(h + i) % n];
        uint64_t cur = slot->hash.load(std::memory_order_relaxed);
        if (cur != h && cur != 0) {
            if (slot->hash.compare_exchange_weak(cur, h, std::memory_order_relaxed)) {
                slot->state.store(BucketState{}, std::memory_order_relaxed);
                return slot;
            }
        }
    }
    return &_table[h % n];                                  // 极端竞争兜底
}

HttpResponse RateLimiter::operator()(const HttpRequest& req, const Next& next) {
    if (!_cfg.enabled)
        return next(req);
    const std::string ip = client_addr(req, _cfg.client_addr_policy);
    if (!ip.empty()) {
        Slot* slot = locate(ip);
        if (!take(slot->state, _cfg.ip_rps, static_cast<double>(_cfg.ip_burst)))
            return denied(_cfg.ip_rps);
    }
    if (!take(_global, _cfg.global_rps, static_cast<double>(_cfg.global_burst)))
        return denied(_cfg.global_rps);
    return next(req);
}

Middleware make_rate_limiter(RateLimitConfig cfg) {
    return Middleware{[r = std::make_shared<RateLimiter>(std::move(cfg))](
                          const HttpRequest& req, const Next& next) {
        return (*r)(req, next);
    }};
}

} // namespace web
```

`HttpCommon.cpp` 的 `reason_phrase` switch 中，`case 413:` 之后加：

```cpp
        case 429: return "Too Many Requests";
```

- [ ] **Step 4: 运行验证通过**

Run: `cmake --build build --target test_middleware -j 8 && ./build/bin/test_middleware.exe`
Expected: 全部通过（含 `test_ratelimit`）。

- [ ] **Step 5: 提交**

```bash
git add src/domain/interface/components/http/RateLimiter.h src/domain/interface/components/http/RateLimiter.cpp src/domain/interface/components/http/HttpCommon.cpp tests/domain/interface/test_middleware.cpp
git commit -m "feat(http): 无锁限流器（每 IP + 全局令牌桶，429 + Retry-After）"
```

---

### Task 5: 访问日志（Combined Log Format，INFO 级）

最外层中间件，记录一切到达的请求；`set_access_log` 默认开启并接入 run()。

**Files:**
- Create: `src/domain/interface/components/http/AccessLog.h`、`AccessLog.cpp`
- Modify: `src/domain/interface/components/http/HttpServer.cpp`（run 组装最外层 + set_access_log 实现已在 Task 2 定义，这里接入默认值）
- Test: `tests/domain/interface/test_middleware.cpp`（新增 TEST_CASE）

- [ ] **Step 1: 写失败测试**

在 `tests/domain/interface/test_middleware.cpp` 的 `} // namespace` 之前追加（文件头加 `#include "domain/interface/components/http/AccessLog.h"`、`#include "common/log/Logger.h"`、`#include "common/log/LogRingBuffer.h"`、`#include <regex>`）：

```cpp
// ---------------------------------------------------------------------------
// 访问日志（直接调用中间件 + 合成请求；经异步 Logger 落 ring 后断言）
// ---------------------------------------------------------------------------
TEST_CASE("test_access_log") {
    auto ring = std::make_shared<LogRingBuffer>(4096);
    Logger::instance().set_ring_buffer(ring);
    auto logger = make_access_logger(ClientAddrPolicy{});

    auto wait_line = [&](const std::string& needle) {
        for (int i = 0; i < 200; ++i) {
            for (const auto& r : ring->snapshot(LogLevel::Info, 4096))
                if (r.message.find(needle) != std::string::npos)
                    return r.message;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return std::string();
    };

    // 正常请求：字段齐全
    HttpRequest req;
    req.remote_addr = "127.0.0.1";
    req.method = Method::Get;
    req.path = "/api/status";
    req.version = "HTTP/1.1";
    logger(req, [](const HttpRequest&) { return json_ok(); });  // body 11 字节
    const std::string line = wait_line("\"GET /api/status HTTP/1.1\" 200");
    expect(!line.empty(), "CLF line logged at INFO");
    if (!line.empty()) {
        expect(line.rfind("127.0.0.1 - - [", 0) == 0, "CLF prefix: ip - - [");
        const size_t lb = line.find('[');
        const size_t rb = line.find(']');
        static const std::regex ts_re(
            R"(^\[[0-9]{2}/[A-Z][a-z]{2}/[0-9]{4}:[0-9]{2}:[0-9]{2}:[0-9]{2} [+-][0-9]{4}\]$)",
            std::regex::ECMAScript);
        expect(lb != std::string::npos && rb != std::string::npos && rb > lb &&
                   std::regex_match(line.substr(lb, rb - lb + 1), ts_re),
               "CLF timestamp [dd/Mon/yyyy:HH:mm:ss +zzzz]");
        expect(line.find("\"GET /api/status HTTP/1.1\" 200 11 \"-\" \"-\"") != std::string::npos,
               "request line + status + bytes; missing ref/ua are dashes");
    }

    // 带 Referer/UA：原样引用
    HttpRequest req2 = req;
    req2.path = "/api/profiles";
    req2.headers.emplace_back("Referer", "http://localhost:18789/");
    req2.headers.emplace_back("User-Agent", "besq-test/1.0");
    logger(req2, [](const HttpRequest&) { return json_ok(); });
    expect(!wait_line("\"GET /api/profiles HTTP/1.1\" 200 11 \"http://localhost:18789/\" \"besq-test/1.0\"")
                .empty(),
           "referer and user-agent quoted in place");

    // 流式响应：bytes = "-"
    HttpRequest req3 = req;
    req3.path = "/api/events";
    logger(req3, [](const HttpRequest&) { return sse_stream_response(); });
    expect(!wait_line("\"GET /api/events HTTP/1.1\" 200 - \"-\" \"-\"").empty(),
           "stream response logs dash bytes");

    // 日志注入消毒：path 控制字符 → '_'
    HttpRequest req4 = req;
    req4.path = "/a\x01b";
    logger(req4, [](const HttpRequest&) { return json_ok(); });
    expect(!wait_line("\"GET /a_b HTTP/1.1\"").empty(), "control chars sanitized");

    // 429 也记（限流器短路在访问日志内侧 → 日志看到 429）
    RateLimitConfig cfg;
    cfg.enabled = true;
    cfg.ip_rps = 1000.0;
    cfg.ip_burst = 0;   // 桶容量 0 → 恒 429
    auto limiter = make_rate_limiter(cfg);
    Next final = [](const HttpRequest&) { return json_ok(); };
    Next inner = [&](const HttpRequest& r) { return limiter(r, final); };
    HttpRequest req5 = req;
    req5.path = "/limited";
    logger(req5, inner);
    expect(!wait_line("\"GET /limited HTTP/1.1\" 429").empty(), "rate-limited request logged with 429");
}
```

- [ ] **Step 2: 运行验证失败**

Run: `cmake --build build --target test_middleware -j 8`
Expected: 编译失败 —— 找不到 `AccessLog.h`。

- [ ] **Step 3: 实现访问日志**

创建 `src/domain/interface/components/http/AccessLog.h`：

```cpp
#pragma once
#include "HttpCommon.h"
#include "Middleware.h"

namespace web {

/// Combined Log Format 访问日志中间件（INFO 级，经全局异步 Logger）：
///   ip - - [dd/Mon/yyyy:HH:mm:ss +zzzz] "METHOD path HTTP/1.1" status bytes "Referer" "UA"
/// 字段对齐 nginx/Apache 惯例；客户端可控字段做日志注入消毒（控制字符→'_'）；
/// is_stream → bytes 记 "-"；next() 异常时记 500 后重抛（防御性）。
/// 客户端 IP 经 client_addr(req, policy) 解析（可信代理 XFF 策略）。
class AccessLogger {
public:
    explicit AccessLogger(ClientAddrPolicy policy) : _policy(std::move(policy)) {}

    HttpResponse operator()(const HttpRequest& req, const Next& next);

private:
    static void log_line(const HttpRequest& req, const HttpResponse& resp);
    static std::string request_line(const HttpRequest& req);
    ClientAddrPolicy _policy;
};

Middleware make_access_logger(ClientAddrPolicy policy = {});

} // namespace web
```

创建 `src/domain/interface/components/http/AccessLog.cpp`：

```cpp
#include "AccessLog.h"
#include "common/log/Logger.h"
#include <chrono>
#include <cstdio>
#include <string>

namespace web {

namespace {

/// 日志消毒：客户端可控字节（path/referer/UA）可能含控制字符 → 替换为 '_'
///（与 Connection::sanitize_for_log 同款，防日志注入）。
std::string sanitize_for_log(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (static_cast<unsigned char>(c) < 0x20) c = '_';
    return out;
}

/// [dd/Mon/yyyy:HH:mm:ss +zzzz]（CLF 惯例；线程安全 localtime 变体）。
std::string clf_timestamp() {
    const std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "[%d/%b/%Y:%H:%M:%S %z]", &tm);
    return buf;
}

/// 引用或 "-"（缺省字段惯例）。
std::string quote_or_dash(const std::string& s) {
    return s.empty() ? "-" : "\"" + sanitize_for_log(s) + "\"";
}

} // namespace

std::string AccessLogger::request_line(const HttpRequest& req) {
    return std::string(method_name(req.method)) + " " + req.path + " " +
           (req.version.empty() ? "HTTP/1.1" : req.version);
}

void AccessLogger::log_line(const HttpRequest& req, const HttpResponse& resp) {
    std::string bytes = "-";
    if (!resp.is_stream && !resp.body.empty())
        bytes = std::to_string(resp.body.size());
    std::string line = (req.remote_addr.empty() ? "-" : req.remote_addr) + " - - " +
                       clf_timestamp() + " \"" + sanitize_for_log(request_line(req)) + "\" " +
                       std::to_string(resp.status) + " " + bytes + " " +
                       quote_or_dash(req.header("Referer")) + " " +
                       quote_or_dash(req.header("User-Agent"));
    Logger::instance().info(std::move(line));
}

HttpResponse AccessLogger::operator()(const HttpRequest& req, const Next& next) {
    try {
        HttpResponse resp = next(req);
        log_line(req, resp);
        return resp;
    } catch (...) {
        // 防御兜底：Router 已把控制器异常映射为响应，正常不会走到；记录 500 后重抛。
        HttpResponse err = HttpResponse::internal_error("middleware chain threw");
        log_line(req, err);
        throw;
    }
}

Middleware make_access_logger(ClientAddrPolicy policy) {
    return AccessLogger(std::move(policy));
}

} // namespace web
```

`HttpServer.cpp`：
- `#include "AccessLog.h"`（`#include "Middleware.h"` 之后）
- `run()` 内 middleware 组装循环之后加（**默认访问日志 = 最外层**，429 也在其内）：

```cpp
    // 默认访问日志（Combined 格式，INFO 级）：位于所有用户中间件之外——
    // 限流 429 等一切到达的请求都记录（nginx 惯例）。set_access_log(false)
    // 关闭；需要可信代理 XFF 策略时自装 make_access_logger(policy)。
    if (impl.access_log)
        dispatch = [m = make_access_logger(), prev = std::move(dispatch)](const HttpRequest& req) {
            return m(req, prev);
        };
```

（`Impl::access_log` 已在 Task 2 置默认 `true`——本任务起默认开启生效。）

- [ ] **Step 4: 运行验证通过**

Run: `cmake --build build --target test_middleware -j 8 && ./build/bin/test_middleware.exe`
Expected: 全部通过（含 `test_access_log`）。

- [ ] **Step 5: 提交**

```bash
git add src/domain/interface/components/http/AccessLog.h src/domain/interface/components/http/AccessLog.cpp src/domain/interface/components/http/HttpServer.cpp tests/domain/interface/test_middleware.cpp
git commit -m "feat(http): Combined Log Format 访问日志（INFO 级，默认最外层开启）"
```

---

### Task 6: 真实 socket 端到端（429 线格式 + 默认日志）与既有套件回归

验证限流+访问日志在真实传输路径上协同：3 连发 → 200/200/429（线格式 reason phrase `Too Many Requests`）、Retry-After、恢复、ring 中有 200 与 429 两行。

**Files:**
- Test: `tests/domain/interface/test_middleware.cpp`（新增 TEST_CASE）
- 回归：`test_http_server` / `test_web_integration` / 其余 http 套件（只运行不改）

- [ ] **Step 1: 写测试**

在 `tests/domain/interface/test_middleware.cpp` 的 `} // namespace` 之前追加：

```cpp
// ---------------------------------------------------------------------------
// 端到端：真实 HttpServer + 限流 + 默认访问日志，真实 socket。
// ---------------------------------------------------------------------------
TEST_CASE("test_middleware_e2e") {
    auto ring = std::make_shared<LogRingBuffer>(4096);
    Logger::instance().set_ring_buffer(ring);
    HttpServer server;
    server.set_handler(Method::Get, "/health",
                       [](const HttpRequest&) { return json_ok(); });
    RateLimitConfig cfg;
    cfg.enabled = true;
    cfg.ip_rps = 1000.0;        // 1 token/ms：恢复测试毫秒级
    cfg.ip_burst = 2;
    cfg.global_rps = 1000000.0;
    cfg.global_burst = 1000000;
    server.use(make_rate_limiter(cfg));   // 默认访问日志自动最外层
    expect(server.start("127.0.0.1", 0), "server starts");
    std::thread srv([&] { server.run(); });
    struct ServerGuard {
        HttpServer& s;
        std::thread& t;
        ~ServerGuard() {
            if (t.joinable()) {
                s.stop();
                t.join();
            }
        }
    } guard{server, srv};

    auto get = [&]() {
        int c = sock_connect("127.0.0.1", server.port());
        expect(c >= 0, "client connects");
        sock_send(c, "GET /health HTTP/1.1\r\nHost: x\r\n\r\n", 3000);
        std::string got;
        sock_recv(c, got, 4096, 3000);
        sock_close(c);
        return got;
    };

    const std::string r1 = get();
    const std::string r2 = get();
    const std::string r3 = get();
    expect(r1.find("200 OK") != std::string::npos, "first request 200");
    expect(r2.find("200 OK") != std::string::npos, "second request 200");
    expect(r3.find("429 Too Many Requests") != std::string::npos, "third request 429 with reason phrase");
    expect(r3.find("Retry-After:") != std::string::npos, "429 carries Retry-After on the wire");

    // 桶恢复（~5ms 后满 5 枚）→ 200
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    expect(get().find("200 OK") != std::string::npos, "recovered after refill");

    // 默认访问日志：200 与 429 都上线（ring 异步落盘，有界等待）
    bool saw_200 = false, saw_429 = false;
    for (int i = 0; i < 200 && !(saw_200 && saw_429); ++i) {
        for (const auto& r : ring->snapshot(LogLevel::Info, 4096)) {
            if (r.message.find("\"GET /health HTTP/1.1\" 200") != std::string::npos)
                saw_200 = true;
            if (r.message.find("\"GET /health HTTP/1.1\" 429") != std::string::npos)
                saw_429 = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(saw_200, "200 logged by default access log");
    expect(saw_429, "429 logged by default access log");
}
```

- [ ] **Step 2: 运行新测试**

Run: `cmake --build build --target test_middleware -j 8 && ./build/bin/test_middleware.exe`
Expected: 全部通过（`Results: N passed, 0 failed`）。

- [ ] **Step 3: 既有套件回归（行为不变，断言数不变）**

Run:
```bash
cmake --build build --target test_http_server test_web_integration test_connection test_socket_nb test_web_solve_sse test_sse_stream test_web_api -j 8
./build/bin/test_http_server.exe --timeout 120
./build/bin/test_web_integration.exe --timeout 120
./build/bin/test_connection.exe && ./build/bin/test_socket_nb.exe
./build/bin/test_web_solve_sse.exe && ./build/bin/test_sse_stream.exe && ./build/bin/test_web_api.exe
```
Expected: 全部 PASS；`test_web_integration` 仍 `92 passed`（默认访问日志只写日志，不改行为）。

- [ ] **Step 4: 提交**

```bash
git add tests/domain/interface/test_middleware.cpp
git commit -m "test(http): 限流+访问日志真实 socket 端到端（429 线格式 + 默认日志上线）"
```

---

### Task 7: GUI 装配（显式开启限流）

**Files:**
- Modify: `src/gui/main.cpp`（`server.set_fallback(...)` 之后、`server.run()` 之前，约 370-399 行）

- [ ] **Step 1: 实现**

`src/gui/main.cpp` 文件头 include 区（`#include "domain/interface/components/http/HttpServer.h"` 之后）加：

```cpp
#include "domain/interface/components/http/RateLimiter.h"
```

在 `server.set_fallback([&](const web::HttpRequest& r) { ... });` 之后、`server.run();` 之前插入：

```cpp
    // 限流（默认关闭；GUI 显式开启，本地宽松阈值）。部署经 Nginx 前置时：
    // rl.client_addr_policy.trust_forwarded = true;（对端恒为 nginx）
    web::RateLimitConfig rl;
    rl.enabled = true;
    server.use(web::make_rate_limiter(rl));
    // 访问日志默认开启（Combined 格式，INFO 级，见 AccessLog.h）。
```

- [ ] **Step 2: 构建 + 冒烟**

Run: `cmake --build build --target besq-gui -j 8 && ./build/bin/besq-gui.exe`（Ctrl+C 退出）
Expected: 构建通过；启动日志出现 `http server start`；前端请求正常（浏览器打开 `http://127.0.0.1:<port>/`，控制台出现 `127.0.0.1 - - [...] "GET / HTTP/1.1" 307 - "-" "-"` 等 INFO 行）。

- [ ] **Step 3: 提交**

```bash
git add src/gui/main.cpp
git commit -m "feat(gui): 显式开启限流（本地宽松阈值，访问日志默认开启）"
```

---

### Task 8: 全量验证 + 格式化 + 提交

**Files:** 无新改动（验证 + 格式化）

- [ ] **Step 1: clang-format 全部改动文件**

Run:
```bash
clang-format -i src/domain/interface/components/http/Middleware.h src/domain/interface/components/http/Middleware.cpp src/domain/interface/components/http/RateLimiter.h src/domain/interface/components/http/RateLimiter.cpp src/domain/interface/components/http/AccessLog.h src/domain/interface/components/http/AccessLog.cpp src/domain/interface/components/http/HttpServer.h src/domain/interface/components/http/HttpServer.cpp src/domain/interface/components/http/HttpCommon.h src/domain/interface/components/http/HttpCommon.cpp src/domain/interface/components/http/HttpParser.cpp src/domain/interface/components/http/Socket.h src/domain/interface/components/http/Socket.cpp src/domain/interface/components/http/Connection.h src/domain/interface/components/http/Connection.cpp tests/domain/interface/test_middleware.cpp tests/domain/interface/test_connection.cpp src/gui/main.cpp
```
Expected: 无报错。

- [ ] **Step 2: 双平台全量构建 + 测试**

Windows：
```bash
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```
Expected: 90+ 项全过（新增 test_middleware 后总数 +1）。

WSL：
```bash
wsl -e bash -c "cd /mnt/f/Dinosaur_MC/Studio/Project/BestEnchSeq-Core/main && flock -n build-wsl/lock -c 'cmake --build build-wsl -j 8' && ctest --test-dir build-wsl --output-on-failure"
```
Expected: 全过。

- [ ] **Step 3: 稳定性抽样**

Run:
```bash
for i in 1 2 3 4 5; do ./build/bin/test_web_integration.exe --timeout 120; done
./build/bin/test_middleware.exe --timeout 120
```
Expected: 全 PASS（访问日志新增 INFO 输出不影响断言）。

- [ ] **Step 4: 提交（若格式化有差异）**

```bash
git add -u && git commit -m "style(http): clang-format 格式化 middleware/ratelimit/accesslog 改动"
```

---

## Self-Review

- **规格覆盖**：middleware 链（Task 2）、限流算法与 429/Retry-After/信封（Task 4）、默认关闭+装配点开启（Task 4/7）、XFF 可信代理（Task 3/4）、Combined 访问日志 INFO 级（Task 5）、429 也记/异常兜底/流式 "-"（Task 5）、解析级 400 不记（设计取舍，文档已标注，无任务）、测试矩阵（Task 1-6）、GUI 装配（Task 7）——逐项有对应任务。
- **占位符扫描**：无 TBD/TODO；每步含完整代码与命令。
- **类型一致性**：`Next`/`Middleware`/`ClientAddrPolicy`/`RateLimitConfig`/`make_rate_limiter`/`make_access_logger` 跨任务签名一致；`client_addr` 在 Task 2 声明（Middleware.h）、Task 3 实现（Middleware.cpp）；`set_access_log` 在 Task 2 声明、Task 5 接入 run() 并默认开启；`Impl::access_log` 默认值 true 于 Task 2 设定、Task 5 生效。
- **注意点**：`RateLimiter` 含 `unique_ptr` 不可拷贝 → `make_rate_limiter` 经 `shared_ptr` 装箱满足 `std::function` 可拷贝约束（Task 4 已实现）；`std::atomic<BucketState>`（16B）以 `static_assert(is_always_lock_free)` 守卫平台可用性（x86-64 cmpxchg16b / ARM64 LDXP）。
