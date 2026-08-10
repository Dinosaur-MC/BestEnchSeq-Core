// =============================================================================
// HTTP middleware 链 + 限流 + 访问日志（components/http）
// =============================================================================
#define BESQ_TEST_MAIN
#include "domain/interface/components/http/HttpServer.h"
#include "domain/interface/components/http/Middleware.h"
#include "domain/interface/components/http/AccessLog.h"
#include "domain/interface/components/http/RateLimiter.h"
#include "domain/interface/components/http/Socket.h"
#include "common/log/Logger.h"
#include "common/log/LogRingBuffer.h"
#include "framework/test_framework.h"
#include <regex>
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

// ---------------------------------------------------------------------------
// 限流器（直接调用中间件，合成 HttpRequest——不走线，确定性测试）
// ---------------------------------------------------------------------------
TEST_CASE("test_ratelimit") {
    RateLimitConfig cfg;
    cfg.enabled = true;
    cfg.ip_rps = 200.0;        // 0.2 token/ms：补 1 枚需 ≥5ms（防时序抖动）；恢复测试 50ms
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

    // 桶恢复：速率 0.2 token/ms → 50ms 补 10 枚（封顶 burst=2）后恢复
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
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

    // 表满淘汰路径：slots=2 + 3 个 IP → 触发替换分支（不崩溃、语义可接受）
    RateLimitConfig cfg4 = cfg;
    cfg4.slots = 2;
    auto rl4 = make_rate_limiter(cfg4);
    served = 0;
    HttpRequest e1;
    e1.remote_addr = "10.0.0.1";
    HttpRequest e2;
    e2.remote_addr = "10.0.0.2";
    HttpRequest e3;
    e3.remote_addr = "10.0.0.3";
    expect(rl4(e1, next).status == 200 && rl4(e2, next).status == 200,
           "slots=2 admits two IPs");
    expect(rl4(e3, next).status == 200, "third IP admitted after eviction");
    expect(rl4(e1, next).status == 200, "evicted IP re-claims a slot");
}

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
    // 注：`"/a\x01" "b"` 而非 "/a\x01b"——后者的 \x01b 是单个十六进制转义
    // 0x1b(ESC)，b 不构成字面字符；相邻字面量拼接才得到 \x01 后跟 'b'。
    HttpRequest req4 = req;
    req4.path = "/a\x01" "b";
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

    // 可信代理：trust_forwarded 下 IP 字段解析为 XFF 最右条目（与限流 key 同一函数）
    auto logger_trust = make_access_logger(ClientAddrPolicy{.trust_forwarded = true});
    HttpRequest reqx = req;
    reqx.path = "/api/trust";
    reqx.headers.emplace_back("X-Forwarded-For", "203.0.113.9");
    logger_trust(reqx, [](const HttpRequest&) { return json_ok(); });
    expect(!wait_line("203.0.113.9 - - [").empty(), "trusted XFF IP in log line");

    // XFF 条目客户端可控：控制字符必须消毒（日志注入面统一）
    // 注：`"203.0.113.9\x01" "evil"` 而非 `"203.0.113.9\x01evil"`——\x01e 会被
    // 十六进制转义贪婪吞掉 'e'（与上方 /a\x01 同款写法）。
    HttpRequest reqy = req;
    reqy.path = "/api/trust2";
    reqy.headers.emplace_back("X-Forwarded-For", "203.0.113.9\x01" "evil");
    logger_trust(reqy, [](const HttpRequest&) { return json_ok(); });
    expect(!wait_line("203.0.113.9_evil - - [").empty(), "XFF control chars sanitized");
}

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
    cfg.ip_rps = 200.0;         // 0.2 token/ms：补 1 枚需 ≥5ms（防真实 socket 往返时序抖动）
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

    // 桶恢复（50ms 补 10 枚，封顶 burst=2）→ 200
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
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

} // namespace
