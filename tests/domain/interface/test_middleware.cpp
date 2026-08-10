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
