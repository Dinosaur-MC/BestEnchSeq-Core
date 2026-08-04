// =============================================================================
// End-to-end: real HttpServer + WebModule + BesqContext over localhost.
// =============================================================================
#include "domain/interface/BesqContext.h"
#include "domain/interface/web/WebModule.h"
#include "domain/interface/web/http/HttpServer.h"
#include "domain/interface/web/http/Socket.h"
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
    // mirrors exactly how besq-gui wires its server (M3.3).  The dispatch
    // result is returned as-is so static assets keep their real Content-Type
    // (re-wrapping via HttpResponse::json would force application/json).
    HttpServer server;
    server.set_fallback([&](const webhttp::HttpRequest& r) {
        return module.dispatch(r.method, r.path, r.body);
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
        expect(root.find("Content-Type: text/html") != std::string::npos,
               "index served as text/html");

        // GET /api/profile
        auto prof = http_exchange(server, "GET /api/profile HTTP/1.1\r\nHost: x\r\n\r\n");
        expect(prof.find("builtin:vanilla") != std::string::npos, "profile list served");

        // POST /api/calculator → poll GET /api/calculator/{id} → completed.
        // Exercises a request BODY + the {id} param route + the async
        // WebSolveService worker through the real stack.
        std::string calc_body =
            "{\"target\":{\"item\":\"diamond_sword\",\"enchants\":"
            "[{\"id\":\"sharpness\",\"level\":5}]},\"algorithm\":\"dp_merge\","
            "\"max_solutions\":1}";
        std::string calc_post =
            "POST /api/calculator HTTP/1.1\r\nHost: x\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(calc_body.size()) + "\r\n\r\n" + calc_body;
        auto cpost = http_exchange(server, calc_post);

        // Extract task_id (best-effort: find "task_id" then the quoted value
        // after ':' — tolerant of the compact serializer's exact spacing).
        std::string task_id;
        auto tid = cpost.find("\"task_id\"");
        if (tid != std::string::npos) {
            auto colon = cpost.find(':', tid);
            auto open = cpost.find('"', colon);
            auto close = open == std::string::npos ? std::string::npos
                                                   : cpost.find('"', open + 1);
            if (close != std::string::npos) task_id = cpost.substr(open + 1, close - open - 1);
        }
        expect(!task_id.empty(), "calculator POST returns a task_id");

        bool completed = false;
        for (int i = 0; i < 500 && !completed; ++i) {
            auto st = http_exchange(server, "GET /api/calculator/" + task_id +
                                                " HTTP/1.1\r\nHost: x\r\n\r\n");
            if (st.find("\"state\":\"completed\"") != std::string::npos) completed = true;
            else std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        expect(completed, "calculator task completes end-to-end");
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
