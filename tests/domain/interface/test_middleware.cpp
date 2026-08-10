// =============================================================================
// HTTP middleware 链 + 限流 + 访问日志（components/http）
// =============================================================================
#define BESQ_TEST_MAIN
#include "domain/interface/components/http/HttpServer.h"
#include "domain/interface/components/http/Middleware.h"
#include "domain/interface/components/http/RateLimiter.h"
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

} // namespace
