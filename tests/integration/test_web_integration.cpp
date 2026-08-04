// =============================================================================
// End-to-end: real HttpServer + WebModule + BesqContext over localhost.
//
// Drives the full §12.1 endpoint-coverage matrix over REAL sockets — no direct
// dispatch, no mocks. The transport path exercised is:
//   raw HTTP bytes → poller → Reactor(EventLoop) → Connection → HttpParser →
//   WebModule.dispatch → controllers → bytes back out the same wire.
//
// Cases:
//  1. GET /                      → 307 + Location: /public/index.html
//  2. GET /public/index.html     → 200 text/html, embedded SPA body
//  3. GET /health, /api/status, /api/settings → 200 + field assertions
//  4. GET /api/profiles (+ meta) → profiles/active + full metadata fields
//  5. POST /api/tasks → 202+task_id+Location → poll to completed+result
//  6. SSE over the real socket   → GET /api/tasks/{id}/events: stream head +
//     a live `event: completed` + `data:` frame (Reactor→Connection→wire)
//  7. Error envelope             → 404 {error,code}; 405 + Allow
//  8. Concurrent clients         → 4 threads each GET /health → all 200
//  9. Path traversal             → GET /public/../secret → 404
//
// All waits are bounded loops; nothing can hang the suite indefinitely.
// =============================================================================

#include "domain/interface/BesqContext.h"
#include "domain/interface/web/WebModule.h"
#include "domain/interface/components/http/HttpServer.h"
#include "domain/interface/components/http/Socket.h"
#include "framework/test_utils.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace web;

namespace {

constexpr const char* kIndexHtml = "<h1>hi</h1>";

// ---------------------------------------------------------------------------
// Raw-socket helpers
// ---------------------------------------------------------------------------

/// One request/response exchange on a fresh connection (blocking recv).
/// Response bodies here are small JSON/static assets, so a single recv with a
/// bounded timeout captures the full reply.
std::string http_exchange(HttpServer& server, const std::string& raw) {
    int c = sock_connect("127.0.0.1", server.port());
    if (c < 0) return "";
    sock_send(c, raw, 3000);
    std::string body;
    sock_recv(c, body, 64 * 1024, 3000);
    sock_close(c);
    return body;
}

/// Non-blocking recv loop: accumulate `got` until `needle` shows up or we run
/// out of tries. Used for the open-ended SSE stream (no Content-Length, the
/// server keeps the connection open).
void recv_until(int client, std::string& got, const char* needle, int max_tries) {
    for (int i = 0; i < max_tries && got.find(needle) == std::string::npos; ++i) {
        std::string c;
        if (sock_recv_nb(client, c, 4096) > 0) got += c;
        else std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

/// Pull `"task_id":"..."` out of a 202 response body (tolerant of the compact
/// serializer's exact spacing: finds the key, then the quoted value).
std::string extract_task_id(const std::string& resp) {
    const auto tid = resp.find("\"task_id\"");
    if (tid == std::string::npos) return "";
    const auto colon = resp.find(':', tid);
    if (colon == std::string::npos) return "";
    const auto open = resp.find('"', colon);
    if (open == std::string::npos) return "";
    const auto close = resp.find('"', open + 1);
    if (close == std::string::npos) return "";
    return resp.substr(open + 1, close - open - 1);
}

/// POST a task body and return the full HTTP response.
std::string post_task(HttpServer& server, const std::string& body) {
    std::string req =
        "POST /api/tasks HTTP/1.1\r\nHost: x\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    return http_exchange(server, req);
}

// ---------------------------------------------------------------------------
// Case 1+2: / redirect + /public static asset
// ---------------------------------------------------------------------------
void test_static_and_root(HttpServer& server) {
    // GET / → 307 + Location: /public/index.html
    auto root = http_exchange(server, "GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(root.find("307") != std::string::npos, "root redirects 307");
    expect(root.find("Location: /public/index.html") != std::string::npos,
           "root Location header");

    // GET /public/index.html → 200 text/html, embedded body
    auto idx = http_exchange(server, "GET /public/index.html HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(idx.find("200 OK") != std::string::npos, "index serves 200");
    expect(idx.find("Content-Type: text/html") != std::string::npos,
           "index content-type text/html");
    expect(idx.find(kIndexHtml) != std::string::npos, "index body served");
}

// ---------------------------------------------------------------------------
// Case 3: health / status / settings
// ---------------------------------------------------------------------------
void test_health_status_settings(HttpServer& server) {
    auto health = http_exchange(server, "GET /health HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(health.find("200 OK") != std::string::npos, "health responds 200");
    expect(health.find("\"status\":\"ok\"") != std::string::npos, "health status field");

    auto status = http_exchange(server, "GET /api/status HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(status.find("200 OK") != std::string::npos, "status responds 200");
    expect(status.find("active_profile") != std::string::npos, "status active_profile");
    expect(status.find("profile_count") != std::string::npos, "status profile_count");
    expect(status.find("algorithm_count") != std::string::npos, "status algorithm_count");
    expect(status.find("has_active_solve") != std::string::npos, "status has_active_solve");
    expect(status.find("uptime_ms") != std::string::npos, "status uptime_ms");

    auto settings = http_exchange(server, "GET /api/settings HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(settings.find("200 OK") != std::string::npos, "settings responds 200");
    expect(settings.find("lang") != std::string::npos, "settings lang field");
    expect(settings.find("log_level") != std::string::npos, "settings log_level field");
}

// ---------------------------------------------------------------------------
// Case 4: profile list + metadata
// ---------------------------------------------------------------------------
void test_profiles(HttpServer& server) {
    auto prof = http_exchange(server, "GET /api/profiles HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(prof.find("200 OK") != std::string::npos, "profiles list 200");
    expect(prof.find("builtin:vanilla") != std::string::npos,
           "profiles list contains builtin:vanilla");
    expect(prof.find("\"active\"") != std::string::npos, "profiles active field");

    auto meta = http_exchange(
        server, "GET /api/profiles/builtin:vanilla HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(meta.find("200 OK") != std::string::npos, "profile metadata 200");
    expect(meta.find("\"name\"") != std::string::npos, "meta name");
    expect(meta.find("\"dependencies\"") != std::string::npos, "meta dependencies");
    expect(meta.find("\"version\"") != std::string::npos, "meta version");
    expect(meta.find("\"release_tag\"") != std::string::npos, "meta release_tag");
    expect(meta.find("\"is_root\"") != std::string::npos, "meta is_root");
    expect(meta.find("\"ench_count\"") != std::string::npos, "meta ench_count");
    expect(meta.find("\"eq_count\"") != std::string::npos, "meta eq_count");
    expect(meta.find("\"tag_count\"") != std::string::npos, "meta tag_count");
    expect(meta.find("\"format\"") != std::string::npos, "meta format");
}

// ---------------------------------------------------------------------------
// Case 5: POST /api/tasks → 202 + task_id + Location → poll to completed
// ---------------------------------------------------------------------------
void test_task_submit_and_poll(HttpServer& server) {
    const std::string body =
        "{\"target\":{\"item\":\"diamond_sword\",\"enchants\":"
        "[{\"id\":\"sharpness\",\"level\":5}]},\"algorithm\":\"dp_merge\","
        "\"max_solutions\":1}";
    auto cpost = post_task(server, body);

    expect(cpost.find("202") != std::string::npos, "task submit returns 202 Accepted");
    expect(cpost.find("Location: /api/tasks/") != std::string::npos, "task Location header");
    const std::string id = extract_task_id(cpost);
    expect(!id.empty(), "task id extracted");

    // Bounded poll: up to 50 × 100ms = 5s. The dp_merge solve on this light
    // target completes in well under a second, so this usually exits after a
    // couple of iterations.
    bool completed = false;
    for (int i = 0; i < 50 && !completed; ++i) {
        auto st = http_exchange(server, "GET /api/tasks/" + id +
                                            " HTTP/1.1\r\nHost: x\r\n\r\n");
        if (st.find("\"state\":\"completed\"") != std::string::npos) {
            completed = true;
            expect(st.find("result") != std::string::npos, "completed task has result");
        } else if (st.find("\"state\":\"failed\"") != std::string::npos) {
            break;  // surfaced by the completed assertion below
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    expect(completed, "task completes end-to-end");
}

// ---------------------------------------------------------------------------
// Case 6: SSE over the real socket
//
// The worker publishes a `progress` frame before solve() and a `completed`
// frame after it. To observe `completed` on the wire the SSE subscription must
// be registered before the solve finishes. We therefore use a moderately heavy
// direct-mode target (chestplate, 4 enchants from 2 source enchants) whose
// dp_merge solve takes on the order of a hundred milliseconds — the SSE
// connect + subscribe lands in ~1ms, so the subscription is guaranteed to be in
// place long before the completed publish. The stream head (200 + text/event-
// stream) and the completed frame are both read from the live socket.
void test_sse_events(HttpServer& server) {
    const std::string body =
        "{\"target\":{\"item\":\"diamond_chestplate\",\"enchants\":["
        "{\"id\":\"protection\",\"level\":4},{\"id\":\"thorns\",\"level\":3},"
        "{\"id\":\"unbreaking\",\"level\":3},{\"id\":\"mending\",\"level\":1}]},"
        "\"source\":[{\"id\":\"protection\",\"level\":3},"
        "{\"id\":\"thorns\",\"level\":2}],\"algorithm\":\"dp_merge\"}";
    auto resp = post_task(server, body);
    const std::string id = extract_task_id(resp);
    expect(!id.empty(), "SSE task created");

    // Fresh client connection for the open-ended stream.
    int c = sock_connect("127.0.0.1", server.port());
    expect(c >= 0, "SSE client connects");
    sock_send(c, "GET /api/tasks/" + id + "/events HTTP/1.1\r\nHost: x\r\n\r\n", 3000);
    set_nonblocking(c);

    std::string got;
    // Stream head: HTTP/1.1 200 + Content-Type: text/event-stream. Arrives
    // within a few ms of connect; 2s budget is generous.
    recv_until(c, got, "text/event-stream", 200);
    expect(got.find("HTTP/1.1 200 OK") != std::string::npos, "SSE stream head status 200");
    expect(got.find("text/event-stream") != std::string::npos, "SSE content-type");
    expect(got.find("Content-Length") == std::string::npos,
           "SSE head has no Content-Length (open-ended stream)");

    // Completed frame: the solve finishes ~100ms+ after the task was submitted;
    // 3s budget covers it with a wide margin.
    recv_until(c, got, "event: completed", 300);
    const auto pos = got.find("event: completed");
    expect(pos != std::string::npos, "SSE completed event frame on the wire");
    if (pos != std::string::npos) {
        expect(got.find("data: ", pos) != std::string::npos,
               "SSE completed frame carries a data payload");
    }

    sock_close(c);
}

// ---------------------------------------------------------------------------
// Case 7: error envelope (404 + 405/Allow)
// ---------------------------------------------------------------------------
void test_error_envelopes(HttpServer& server) {
    auto nope = http_exchange(server, "GET /nope HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(nope.find("404") != std::string::npos, "unknown path returns 404");
    expect(nope.find("\"error\"") != std::string::npos, "404 error envelope");
    expect(nope.find("\"code\"") != std::string::npos, "404 error code field");

    auto del = http_exchange(server, "DELETE /api/settings HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(del.find("405") != std::string::npos, "DELETE settings returns 405");
    expect(del.find("Allow:") != std::string::npos, "405 Allow header present");
}

// ---------------------------------------------------------------------------
// Case 8: concurrent clients — 4 threads each GET /health → all 200
// ---------------------------------------------------------------------------
void test_concurrent_clients(HttpServer& server) {
    std::atomic<int> ok{0};
    std::vector<std::thread> clients;
    for (int i = 0; i < 4; ++i) {
        clients.emplace_back([&] {
            int c = sock_connect("127.0.0.1", server.port());
            if (c < 0) return;
            sock_send(c, "GET /health HTTP/1.1\r\nHost: x\r\n\r\n", 3000);
            std::string got;
            sock_recv(c, got, 16 * 1024, 3000);
            if (got.find("200 OK") != std::string::npos &&
                got.find("status") != std::string::npos)
                ++ok;
            sock_close(c);
        });
    }
    for (auto& t : clients) t.join();
    expect(ok.load() == 4, "all 4 concurrent clients got a 200 response");
}

// ---------------------------------------------------------------------------
// Case 9: path traversal under /public
// ---------------------------------------------------------------------------
void test_path_traversal(HttpServer& server) {
    auto tr = http_exchange(server, "GET /public/../secret HTTP/1.1\r\nHost: x\r\n\r\n");
    expect(tr.find("404") != std::string::npos, "traversal request blocked 404");
    expect(tr.find("secret") == std::string::npos, "no file content leaked");
}

} // namespace

// ---------------------------------------------------------------------------
// Suite: build the in-process stack mirroring besq-gui, then drive it with raw
// sockets. server is declared after module so it is destroyed first (WebModule
// dtor ordering invariant).
// ---------------------------------------------------------------------------
static void run_suite() {
    BesqContext ctx;
    ctx.load_builtin();
    ctx.load_profiles();

    WebModule module(ctx);
    module.set_static_resources({
        {"/index.html", {"text/html", kIndexHtml}},
    });

    HttpServer server;
    server.set_fallback([&](const HttpRequest& r) { return module.dispatch(r); });
    expect(server.start("127.0.0.1", 0), "server starts");
    std::thread server_thread([&] { server.run(); });

    try {
        test_static_and_root(server);
        test_health_status_settings(server);
        test_profiles(server);
        test_error_envelopes(server);
        test_task_submit_and_poll(server);
        test_sse_events(server);
        test_concurrent_clients(server);
        test_path_traversal(server);
    } catch (...) {
        // A stray expect failure must not leave the accept-loop thread joinable.
        server.stop();
        server_thread.join();
        throw;
    }

    server.stop();
    server_thread.join();
}

int main() {
    try {
        run_suite();
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }
    TEST_PASS("test_web_integration");
    return print_summary();
}
