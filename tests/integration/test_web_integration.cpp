// =============================================================================
// End-to-end: real HttpServer + WebModule + BesqContext over localhost.
// =============================================================================
#include "domain/interface/BesqContext.h"
#include "domain/interface/web/WebModule.h"
#include "domain/interface/web/http/HttpServer.h"
#include "domain/interface/web/http/Socket.h"
#include "domain/interface/web/WebSchema.h"
#include "framework/test_utils.h"
#include <chrono>
#include <string>
#include <thread>

using webhttp::HttpServer;
using webhttp::WebModule;

static std::string http_exchange(HttpServer& server, const std::string& raw) {
    int c = webhttp::sock_connect("127.0.0.1", server.port());
    if (c < 0) return "";
    webhttp::sock_send(c, raw, 3000);
    std::string body;
    webhttp::sock_recv(c, body, 64 * 1024, 3000);
    webhttp::sock_close(c);
    return body;
}

void test_web_end_to_end() {
    BesqContext ctx;
    ctx.load_builtin();
    WebModule module(ctx);
    module.set_static_resources({
        {"/index.html", {"text/html", "<h1>BestEnchSeq</h1>"}},
        {"/app.js", {"text/javascript", "console.log('hi')"}},
    });

    // Route everything through WebModule: /health, /api/* and static assets
    // all dispatch from module.dispatch with the raw method/path/body.  This
    // mirrors exactly how besq-gui wires its server (M3.3).
    HttpServer server;
    server.set_fallback([&](const webhttp::HttpRequest& r) {
        auto resp = module.dispatch(r.method, r.path, r.body);
        return webhttp::HttpResponse::json(resp.status, resp.reason, resp.body);
    });
    expect(server.start("127.0.0.1", 0), "server starts");
    std::thread server_thread([&] { server.run(); });

    try {
        // GET /health
        auto health = http_exchange(server, "GET /health HTTP/1.1\r\nHost: x\r\n\r\n");
        expect(health.find("200 OK") != std::string::npos, "health responds 200");

        // GET / → index.html
        auto root = http_exchange(server, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
        expect(root.find("<h1>BestEnchSeq</h1>") != std::string::npos, "root serves index");

        // GET /api/profile
        auto prof = http_exchange(server, "GET /api/profile HTTP/1.1\r\nHost: x\r\n\r\n");
        expect(prof.find("builtin:vanilla") != std::string::npos, "profile list served");
    } catch (...) {
        // A stray expect failure must not leave the accept-loop thread joinable.
        server.stop();
        server_thread.join();
        throw;
    }

    server.stop();
    server_thread.join();
    TEST_PASS("web end-to-end");
}

int main() {
    try {
        test_web_end_to_end();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
