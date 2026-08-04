// =============================================================================
// HttpServer 多 Reactor 传输测试（Poller 桥 + N×EventLoop 分片，namespace web）
//
// 端到端验证：
//   - 8 个并发客户端同时 GET /health → 每个都收到 200 响应（分片 Reactor 各自
//     处理自己归属的连接，互不阻塞）；
//   - stop() 后 run() 正常返回（优雅关闭：stop accept → 各 Reactor 关连接 → join）。
// =============================================================================
#include "domain/interface/components/http/HttpServer.h"
#include "domain/interface/components/http/Socket.h"
#include "framework/test_utils.h"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace web;

int main() {
    HttpServer server;
    server.set_handler(Method::Get, "/health",
                       [](const HttpRequest&) { return HttpResponse::json(200, "OK", R"({"status":"ok"})"); });
    expect(server.start("127.0.0.1", 0), "server starts on ephemeral port");
    expect(server.port() > 0, "server reports bound port");

    std::thread srv([&] { server.run(); });

    // 8 个并发客户端同时 GET /health
    std::atomic<int> ok{0};
    std::vector<std::thread> clients;
    for (int i = 0; i < 8; ++i)
        clients.emplace_back([&] {
            int c = sock_connect("127.0.0.1", server.port());
            std::string req = "GET /health HTTP/1.1\r\nHost: x\r\n\r\n";
            sock_send(c, req);
            std::string got;
            for (int j = 0; j < 100 && got.find("status") == std::string::npos; ++j) {
                std::string chunk;
                if (sock_recv(c, chunk, 4096, 200) > 0)
                    got += chunk;
            }
            if (got.find("200 OK") != std::string::npos && got.find("\"status\"") != std::string::npos)
                ++ok;
            sock_close(c);
        });
    for (auto& t : clients)
        t.join();

    expect(ok.load() == 8, "all 8 concurrent clients got a 200 response");

    server.stop();
    srv.join();
    TEST_PASS("test_http_server");
    return print_summary();
}
