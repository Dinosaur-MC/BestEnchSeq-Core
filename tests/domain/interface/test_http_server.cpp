// =============================================================================
// HttpServer 多 Reactor 传输测试（Poller 桥 + N×EventLoop 分片，namespace web）
//
// 端到端验证：
//   - 8 个并发客户端同时 GET /health → 每个都收到 200 响应（分片 Reactor 各自
//     处理自己归属的连接，互不阻塞）；
//   - 准入上限 = min(256, FD_SETSIZE)（I-3 accept-cap 修复）：超过上限的连接
//     accept 后立即关闭（不响应任何字节），恰好 1 个客户端观察到 EOF；
//   - stop() 后 run() 正常返回（优雅关闭：stop accept → 各 Reactor 关连接 → join）。
// =============================================================================
#include "domain/interface/components/http/HttpServer.h"
#include "domain/interface/components/http/Socket.h"
#include "framework/test_utils.h"
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

using namespace web;

/// 与服务端一致的准入上限：min(256, FD_SETSIZE)。
constexpr size_t kAdmitCap = (FD_SETSIZE < 256) ? static_cast<size_t>(FD_SETSIZE) : 256;

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

    // ── I-3 accept-cap：准入上限 = min(256, FD_SETSIZE)，超过的连接被 accept 后
    // 立即关闭（无任何响应字节）。打开 kAdmitCap + 1 个连接，保持空闲：其中恰好
    // 1 个会观察到 EOF（服务器拒绝），其余 kAdmitCap 个保持打开（被轮询，未挂起）。
    const size_t kTotal = kAdmitCap + 1;
    std::vector<int> cs;
    cs.reserve(kTotal);
    for (size_t i = 0; i < kTotal; ++i) {
        int c = sock_connect("127.0.0.1", server.port());
        expect(c >= 0, "cap test client connects");
        set_nonblocking(c);
        cs.push_back(c);
    }
    // 有界等待：服务端每 ~100ms 批处理 accept；8s 预算内必处理完 kTotal 个连接。
    size_t eof_count = 0;
    bool saw_eof = false;
    for (int round = 0; round < 800 && !saw_eof; ++round) {
        eof_count = 0;
        for (int c : cs) {
            // wait_readable==1（可读/错误）+ recv 0 字节 = 对端 EOF。
            if (wait_readable(c, 0) == 1) {
                std::string chunk;
                if (sock_recv_nb(c, chunk, 4096) == 0) ++eof_count;  // EOF，无字节
            }
        }
        if (eof_count > 0) saw_eof = true;
        else std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    expect(saw_eof, "server rejected the over-cap connection (EOF observed)");
    expect(eof_count == 1, "exactly one over-cap connection was closed");
    // 其余被准入的连接必须仍在线（服务器没有拒绝它们，也没有错误关闭）。
    // 对称判定：可读 + recv 0 字节 = EOF（被拒）；否则 = 仍打开且被轮询。
    size_t alive = 0;
    for (int c : cs) {
        if (wait_readable(c, 0) == 1) {
            std::string chunk;
            if (sock_recv_nb(c, chunk, 4096) == 0) continue;  // EOF → 被拒连接
        }
        ++alive;
    }
    expect(alive == kAdmitCap, "all admitted connections remain open and polled");
    for (int c : cs) sock_close(c);

    server.stop();
    srv.join();
    TEST_PASS("test_http_server");
    return print_summary();
}
